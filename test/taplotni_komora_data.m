%% load_and_sync.m
% Loads AQM data (CSV) and temperature chamber data (XLSX),
% corrects AQM time offset (+2h 10min), trims both datasets to 11:25-15:25,
% interpolates chamber data to 1s AQM grid
% and stores the result as timetable 'T'.

clear;
clc;
close all

%% ── 1. SETTINGS ───────────────────────────────────────────────────────────
csv_file  = 'csv_data/aqm_data_2026_04_24.csv';
xlsx_file = 'CTS_temperature_chamber_data_2026_04_24_15_25.xlsx';

aqm_time_offset = hours(2) + minutes(10);   % AQM is 2h 10min behind → shift forward

% Measurement window (real time)
t_start = datetime(2026, 4, 24, 11, 25, 0);
t_end   = datetime(2026, 4, 24, 15, 25, 0);

%% ── 2. LOAD AQM CSV ───────────────────────────────────────────────────────
opts = detectImportOptions(csv_file, 'Delimiter', ';');
opts.VariableNamingRule = 'preserve';
opts = setvaropts(opts, 'Timestamp', 'InputFormat', 'yyyy-MM-dd HH:mm:ss.SSS');

aqm_raw = readtimetable(csv_file, opts, 'RowTimes', 'Timestamp');
aqm_raw.Properties.DimensionNames{1} = 'Time';

% Apply time offset correction
aqm_raw.Time = aqm_raw.Time + aqm_time_offset;

% Trim to measurement window
aqm = aqm_raw(timerange(t_start, t_end, 'closed'), :);

fprintf('AQM: %d rows, %s → %s\n', height(aqm), ...
    string(aqm.Time(1)), string(aqm.Time(end)));

%% ── 3. LOAD TEMPERATURE CHAMBER DATA (XLSX) ───────────────────────────────
cts_raw = readtimetable(xlsx_file, 'RowTimes', 'Timestamp');
cts_raw.Properties.DimensionNames{1} = 'Time';

% Trim to measurement window
cts = cts_raw(timerange(t_start, t_end, 'closed'), :);

fprintf('Chamber: %d rows, %s → %s\n', height(cts), ...
    string(cts.Time(1)), string(cts.Time(end)));

%% ── 4. INTERPOLATE CHAMBER DATA TO 1s AQM GRID ───────────────────────────
% Convert timestamps to seconds from t_start for interp1
t_cts_s = seconds(cts.Time - t_start);
t_aqm_s = seconds(aqm.Time - t_start);

actual_interp = interp1(t_cts_s, cts.("Actual_C"), t_aqm_s, 'linear',  'extrap');
target_interp = interp1(t_cts_s, cts.("Target_C"), t_aqm_s, 'nearest', 'extrap');

%% ── 5. BUILD MERGED TIMETABLE ─────────────────────────────────────────────
T = aqm;
T.chamber_actual_degC = actual_interp;
T.chamber_target_degC = target_interp;

fprintf('Result timetable T: %d rows, %d variables\n', height(T), width(T));

%% ── 6. OVERVIEW PLOT ──────────────────────────────────────────────────────
figure;
yyaxis left
plot(T.Time, T.chamber_actual_degC, 'r-', 'LineWidth', 1.5);
ylabel('Chamber temperature actual (°C)');
hold on

yyaxis left
plot(T.Time, T.chamber_target_degC, 'c-', 'LineWidth', 1.5);
ylabel('Chamber temperature (°C)');

yyaxis right
plot(T.Time, T.temperature, 'b-', 'LineWidth', 1);
ylabel('AQM sensor temperature (°C)');

xlabel('Time');
title('Synchronized data - temperature chamber vs. AQM');
legend('Chamber (actual)', 'Chamber (target)', 'AQM temperature');
grid on;




%% temperature_analysis.m
% Analyzes temperature dependencies of AQM sensor variables.
% Requires timetable 'T' in workspace (output of taplotni_komora_data.m).
% Reference temperature: 20 °C

T_ref = 20;  % Reference temperature for compensation (°C)

%% ── 1. CLASSIFY DATA POINTS: RAMP UP / RAMP DOWN / STEADY-STATE ──────────
% Compute rate of change of chamber temperature (°C/min)
dt_min   = seconds(diff(T.Time)) / 60;
dT_dt    = diff(T.chamber_actual_degC) ./ dt_min;
dT_dt    = [dT_dt; dT_dt(end)];  % Pad last value to match length

ramp_threshold = 0.5;  % °C/min — below this is considered steady-state

idx_up     = dT_dt >  ramp_threshold;
idx_down   = dT_dt < -ramp_threshold;
idx_steady = ~idx_up & ~idx_down;

T.phase = repmat("steady", height(T), 1);
T.phase(idx_up)   = "ramp_up";
T.phase(idx_down) = "ramp_down";

fprintf('Ramp up:      %d s\n', sum(idx_up));
fprintf('Ramp down:    %d s\n', sum(idx_down));
fprintf('Steady-state: %d s\n', sum(idx_steady));

%% ── 1b. MICS VALIDITY MASK ────────────────────────────────────────────────
% MICS shows artefact peaks during fast ramp-down (likely surface condensation
% or heater thermal lag). These samples are excluded from fitting.
% Mask criteria: ramp_down AND chamber temperature below threshold,
% or R/R0 exceeds physical limit.
mics_valid_T_threshold = 5;   % °C — flag invalid below this on ramp down
mics_rr_limit          = 2.0; % R/R0 limit — flag outliers above this

% T_chamber defined here for use in validity mask (also used later in sec. 3)
T_chamber = T.chamber_actual_degC;

% Preliminary R/R0 for masking (using raw R0 reference values)
rr_red  = T.mics_red_r  ./ T.mics_red_r0;
rr_ox   = T.mics_ox_r   ./ T.mics_ox_r0;
rr_nh3  = T.mics_nh3_r  ./ T.mics_nh3_r0;

% Valid: not (ramp_down below threshold) AND all R/R0 within limits
T.mics_valid = ~(idx_down & T_chamber < mics_valid_T_threshold) & ...
               (rr_red < mics_rr_limit) & ...
               (rr_ox  < mics_rr_limit) & ...
               (rr_nh3 < mics_rr_limit);

fprintf('MICS valid samples: %d / %d (%.1f %%)\n', ...
    sum(T.mics_valid), height(T), 100*mean(T.mics_valid));

%% ── 2. DEFINE SENSOR GROUPS ───────────────────────────────────────────────
% Each group: {variable names, fit type, y-axis label}
groups(1).vars   = {'mics_red_r', 'mics_ox_r', 'mics_nh3_r'};
groups(1).labels = {'MICS RED R (Ω)', 'MICS OX R (Ω)', 'MICS NH3 R (Ω)'};
groups(1).fit    = 'exp';   % Exponential fit: a*exp(b*T)
groups(1).title  = 'MICS sensor resistances';

groups(2).vars   = {'temperature', 'humidity'};
groups(2).labels = {'SEN55 Temperature (°C)', 'SEN55 Humidity (%RH)'};
groups(2).fit    = 'poly1';
groups(2).title  = 'SEN55 - Temperature & Humidity';

groups(3).vars   = {'voc_index', 'nox_index'};
groups(3).labels = {'VOC Index', 'NOx Index'};
groups(3).fit    = 'poly2';
groups(3).title  = 'SEN55 - VOC & NOx index';

groups(4).vars   = {'pm1_0', 'pm2_5', 'pm4_0', 'pm10_0'};
groups(4).labels = {'PM1.0 (µg/m³)', 'PM2.5 (µg/m³)', 'PM4.0 (µg/m³)', 'PM10.0 (µg/m³)'};
groups(4).fit    = 'poly2';
groups(4).title  = 'SEN55 - Particulate matter';

groups(5).vars   = {'so2_ppm', 'h2s_ppm'};
groups(5).labels = {'SO2 (ppm)', 'H2S (ppm)'};
groups(5).fit    = 'poly2';
groups(5).title  = 'Gas sensors - SO2 & H2S';

groups(6).vars   = {'v3v3_val', 'v5v_val'};
groups(6).labels = {'3.3V rail (V)', '5V rail (V)'};
groups(6).fit    = 'poly1';
groups(6).title  = 'Power supply rails';

%% ── 3. FIT & PLOT EACH GROUP ──────────────────────────────────────────────
% T_chamber already defined in section 1b
colors     = lines(4);  % Color palette for variables within a group
fit_results = struct();  % Store fit coefficients for compensation

for g = 1:numel(groups)
    grp    = groups(g);
    nvars  = numel(grp.vars);
    figure('Name', grp.title, 'NumberTitle', 'off');
    sgtitle(grp.title);

    for v = 1:nvars
        varname = grp.vars{v};
        y       = T.(varname);

        % Skip if variable not found in T
        if ~ismember(varname, T.Properties.VariableNames)
            warning('Variable %s not found in T, skipping.', varname);
            continue;
        end

        subplot(nvars, 1, v);
        hold on;

        % Scatter: ramp down, ramp up, steady-state (+ invalid points for MICS)
        if g == 1
            idx_invalid = ~T.mics_valid;
            scatter(T_chamber(idx_invalid), y(idx_invalid), 2, [0.7 0.7 0.7], 'filled', 'DisplayName', 'Invalid (masked)');
        end
        scatter(T_chamber(idx_down),   y(idx_down),   2, [0.2 0.6 1.0], 'filled', 'DisplayName', 'Ramp down');
        scatter(T_chamber(idx_up),     y(idx_up),     2, [1.0 0.4 0.2], 'filled', 'DisplayName', 'Ramp up');
        scatter(T_chamber(idx_steady), y(idx_steady), 4, [0.2 0.8 0.2], 'filled', 'DisplayName', 'Steady-state');

        % Compute and plot fit — MICS group uses validity mask
        if g == 1
            fit_mask = T.mics_valid;  % Exclude artefact peaks for MICS
        else
            fit_mask = true(height(T), 1);
        end
        T_vec  = T_chamber(fit_mask);
        y_vec  = y(fit_mask);
        T_fit  = linspace(min(T_chamber), max(T_chamber), 500)';

        switch grp.fit
            case 'exp'
                % Fit ln(y) = ln(a) + b*T  (linear in log space)
                valid = y_vec > 0;
                p     = polyfit(T_vec(valid), log(y_vec(valid)), 1);
                y_fit = exp(p(2)) * exp(p(1) * T_fit);
                fit_results.(varname).type = 'exp';
                fit_results.(varname).p    = p;
                fit_label = sprintf('Fit: %.2e·exp(%.4f·T)', exp(p(2)), p(1));

            case 'poly1'
                p     = polyfit(T_vec, y_vec, 1);
                y_fit = polyval(p, T_fit);
                fit_results.(varname).type = 'poly1';
                fit_results.(varname).p    = p;
                fit_label = sprintf('Fit: %.4f·T + %.4f', p(1), p(2));

            case 'poly2'
                p     = polyfit(T_vec, y_vec, 2);
                y_fit = polyval(p, T_fit);
                fit_results.(varname).type = 'poly2';
                fit_results.(varname).p    = p;
                fit_label = sprintf('Fit: %.4f·T² + %.4f·T + %.4f', p(1), p(2), p(3));
        end

        plot(T_fit, y_fit, 'k-', 'LineWidth', 2, 'DisplayName', fit_label);

        ylabel(grp.labels{v});
        xlabel('Chamber temperature (°C)');
        legend('Location', 'best', 'FontSize', 7);
        grid on;
        hold off;
    end
end

%% ── 4. TEMPERATURE COMPENSATION ──────────────────────────────────────────
% For each fitted variable:
%   compensated = measured - f(T_actual) + f(T_ref)
% This removes the temperature-induced offset relative to T_ref.

fprintf('\n── Applying temperature compensation (T_ref = %d °C) ──\n', T_ref);

vars_to_compensate = fieldnames(fit_results);
for i = 1:numel(vars_to_compensate)
    varname = vars_to_compensate{i};
    fr      = fit_results.(varname);
    y_meas  = T.(varname);

    switch fr.type
        case 'exp'
            f_T     = exp(fr.p(2)) * exp(fr.p(1) * T_chamber);
            f_Tref  = exp(fr.p(2)) * exp(fr.p(1) * T_ref);
        case {'poly1', 'poly2'}
            f_T     = polyval(fr.p, T_chamber);
            f_Tref  = polyval(fr.p, T_ref);
    end

    T.([varname '_comp']) = y_meas - f_T + f_Tref;
    fprintf('  Compensated: %s → %s_comp\n', varname, varname);
end

fprintf('\nDone. Compensated variables added to timetable T with suffix _comp.\n');

%% ── 5. EXAMPLE: PLOT ORIGINAL VS COMPENSATED FOR MICS ───────────────────
mics_vars = {'mics_red_r', 'mics_ox_r', 'mics_nh3_r'};
mics_labels = {'MICS RED R (Ω)', 'MICS OX R (Ω)', 'MICS NH3 R (Ω)'};

figure('Name', 'MICS compensation result', 'NumberTitle', 'off');
sgtitle('MICS resistances: original vs. temperature compensated');

for v = 1:numel(mics_vars)
    varname = mics_vars{v};
    subplot(numel(mics_vars), 1, v);
    hold on;
    plot(T.Time, T.(varname),              'r-', 'LineWidth', 0.8, 'DisplayName', 'Original');
    plot(T.Time, T.([varname '_comp']),    'b-', 'LineWidth', 0.8, 'DisplayName', 'Compensated');
    yyaxis right
    plot(T.Time, T.chamber_actual_degC, 'k--', 'LineWidth', 0.5, 'DisplayName', 'Chamber T');
    ylabel('Chamber T (°C)');
    yyaxis left
    ylabel(mics_labels{v});
    legend('Location', 'best', 'FontSize', 7);
    grid on;
    hold off;
end
xlabel('Time');

%% ── 6. MICS R/R0 VALIDATION ──────────────────────────────────────────────
% In clean air: R/R0 should be ≈ 1 regardless of temperature.
% Compare R/R0 before and after compensation to validate the correction.

mics_r0 = {'mics_red_r0', 'mics_ox_r0', 'mics_nh3_r0'};

figure('Name', 'MICS R/R0 validation', 'NumberTitle', 'off');
sgtitle('MICS R/R0: original vs. compensated (clean air → target ≈ 1)');

for v = 1:numel(mics_vars)
    varname  = mics_vars{v};
    r0_name  = mics_r0{v};
    R0       = T.(r0_name);

    ratio_orig = T.(varname)              ./ R0;
    ratio_comp = T.([varname '_comp'])    ./ R0;

    subplot(numel(mics_vars), 1, v);
    hold on;

    % Plot R/R0 vs chamber temperature (scatter, colored by phase)
    scatter(T_chamber(idx_down),   ratio_orig(idx_down),   2, [0.2 0.6 1.0], 'filled', 'DisplayName', 'Original ramp down');
    scatter(T_chamber(idx_up),     ratio_orig(idx_up),     2, [1.0 0.4 0.2], 'filled', 'DisplayName', 'Original ramp up');
    scatter(T_chamber(idx_steady), ratio_orig(idx_steady), 4, [0.8 0.2 0.8], 'filled', 'DisplayName', 'Original steady');
    scatter(T_chamber,             ratio_comp,             1, [0.2 0.8 0.2], 'filled', 'DisplayName', 'Compensated');

    % Reference line at R/R0 = 1
    yline(1, 'k--', 'LineWidth', 1.5, 'DisplayName', 'R/R0 = 1');

    ylabel(sprintf('%s / %s (-)', varname, r0_name));
    xlabel('Chamber temperature (°C)');
    legend('Location', 'best', 'FontSize', 7);
    grid on;
    hold off;
end

% Plot R/R0 over time with both chamber temperature and humidity
figure('Name', 'MICS R/R0 over time', 'NumberTitle', 'off');
sgtitle('MICS R/R0 over time: original vs. compensated (T + humidity overlay)');

for v = 1:numel(mics_vars)
    varname  = mics_vars{v};
    r0_name  = mics_r0{v};
    R0       = T.(r0_name);

    ratio_orig = T.(varname)           ./ R0;
    ratio_comp = T.([varname '_comp']) ./ R0;

    subplot(numel(mics_vars), 1, v);
    hold on;
    plot(T.Time, ratio_orig, 'r-',  'LineWidth', 0.8, 'DisplayName', 'Original R/R0');
    plot(T.Time, ratio_comp, 'b-',  'LineWidth', 0.8, 'DisplayName', 'Compensated R/R0 (T only)');
    yline(1, 'k--', 'LineWidth', 1.5, 'DisplayName', 'R/R0 = 1');
    yyaxis right
    plot(T.Time, T.chamber_actual_degC, 'k:',  'LineWidth', 0.8, 'DisplayName', 'Chamber T (°C)');
    plot(T.Time, T.humidity,            'm--', 'LineWidth', 0.8, 'DisplayName', 'Humidity (%RH)');
    ylabel('Chamber T (°C) / Humidity (%RH)');
    yyaxis left
    ylabel(sprintf('%s / %s (-)', varname, r0_name));
    legend('Location', 'best', 'FontSize', 7);
    grid on;
    hold off;
end
xlabel('Time');

%% ── 7. MICS 2D COMPENSATION: TEMPERATURE + HUMIDITY ─────────────────────
% Model: R = a*exp(b*T) * (c*RH + d)
% Fitted in log space: ln(R) = ln(a) + b*T + ln(c*RH + d)
% Simplified approach: fit residuals from T-only compensation vs. humidity
% R_resid = R - f(T)  →  fit R_resid vs. humidity (poly1)
% Full compensation: R_comp2D = R - f(T) - g(RH) + f(T_ref) + g(RH_ref)

RH_ref = 50;  % Reference humidity for compensation (%RH) — typical indoor value

fprintf('\n── Applying 2D compensation: T + humidity (RH_ref = %d %%RH) ──\n', RH_ref);

figure('Name', 'MICS 2D compensation (T + RH)', 'NumberTitle', 'off');
sgtitle('MICS R/R0: original vs. T-only vs. T+RH compensated');

for v = 1:numel(mics_vars)
    varname = mics_vars{v};
    r0_name = mics_r0{v};
    R0      = T.(r0_name);
    y_meas  = T.(varname);
    RH      = T.humidity;
    fr      = fit_results.(varname);  % Already fitted T-dependency

    % Step 1: Remove T-dependency to get residuals (on valid samples only)
    f_T     = exp(fr.p(2)) * exp(fr.p(1) * T_chamber);
    f_Tref  = exp(fr.p(2)) * exp(fr.p(1) * T_ref);
    R_resid = y_meas - f_T;

    % Step 2: Fit residuals vs. humidity on valid samples only
    valid_mask = T.mics_valid;
    p_rh    = polyfit(RH(valid_mask), R_resid(valid_mask), 1);
    g_RH    = polyval(p_rh, RH);
    g_RHref = polyval(p_rh, RH_ref);

    fprintf('  %s humidity fit: %.2f * RH + %.2f\n', varname, p_rh(1), p_rh(2));

    % Step 3: Full 2D compensation
    R_comp2D = y_meas - f_T - g_RH + f_Tref + g_RHref;
    T.([varname '_comp2D']) = R_comp2D;

    % Plot comparison
    ratio_orig   = y_meas                  ./ R0;
    ratio_comp1D = T.([varname '_comp'])   ./ R0;
    ratio_comp2D = R_comp2D                ./ R0;

    subplot(numel(mics_vars), 1, v);
    hold on;
    plot(T.Time, ratio_orig,   'r-', 'LineWidth', 0.8, 'DisplayName', 'Original R/R0');
    plot(T.Time, ratio_comp1D, 'b-', 'LineWidth', 0.8, 'DisplayName', 'T-only compensated');
    plot(T.Time, ratio_comp2D, 'g-', 'LineWidth', 0.8, 'DisplayName', 'T+RH compensated');
    yline(1, 'k--', 'LineWidth', 1.5, 'DisplayName', 'R/R0 = 1');
    yyaxis right
    plot(T.Time, T.chamber_actual_degC, 'k:',  'LineWidth', 0.5, 'DisplayName', 'Chamber T');
    plot(T.Time, T.humidity,            'm--', 'LineWidth', 0.5, 'DisplayName', 'Humidity');
    ylabel('Chamber T (°C) / Humidity (%RH)');
    yyaxis left
    ylabel(sprintf('%s / %s (-)', varname, r0_name));
    legend('Location', 'best', 'FontSize', 7);
    grid on;
    hold off;
end
xlabel('Time');

fprintf('\nDone. 2D compensated variables added to T with suffix _comp2D.\n');

%% ── 8. MULTIPLICATIVE MODEL: R = a · exp(b·T) · exp(c·RH) ───────────────
% Fit in log space: ln(R) = ln(a) + b·T + c·RH
% This is a proper joint model — T and RH fitted simultaneously.
% Compensation: R_mult = R / (exp(b·T) · exp(c·RH)) · (exp(b·T_ref) · exp(c·RH_ref))

fprintf('\n── Multiplicative model: R = a·exp(b·T)·exp(c·RH) ──\n');

figure('Name', 'MICS multiplicative model (T+RH)', 'NumberTitle', 'off');
sgtitle('MICS R/R0: multiplicative T+RH model compensation');

for v = 1:numel(mics_vars)
    varname = mics_vars{v};
    r0_name = mics_r0{v};
    R0      = T.(r0_name);
    y_meas  = T.(varname);
    RH      = T.humidity;

    % Fit on valid samples only
    valid_mask = T.mics_valid;
    y_fit_in   = y_meas(valid_mask);
    T_fit_in   = T_chamber(valid_mask);
    RH_fit_in  = RH(valid_mask);

    % Multiple linear regression in log space: [1, T, RH] * [ln(a); b; c]
    X   = [ones(sum(valid_mask),1), T_fit_in, RH_fit_in];
    lnR = log(y_fit_in);
    coeff = X \ lnR;  % Least squares solution
    ln_a = coeff(1);
    b    = coeff(2);
    c    = coeff(3);

    fprintf('  %s: a=%.3e, b=%.5f, c=%.5f\n', varname, exp(ln_a), b, c);

    % Apply multiplicative compensation
    f_mult     = exp(ln_a) .* exp(b .* T_chamber) .* exp(c .* RH);
    f_mult_ref = exp(ln_a) .* exp(b .* T_ref)     .* exp(c .* RH_ref);
    R_mult     = y_meas .* (f_mult_ref ./ f_mult);
    T.([varname '_comp_mult']) = R_mult;

    % Store for comparison
    ratio_orig   = y_meas                        ./ R0;
    ratio_comp1D = T.([varname '_comp'])         ./ R0;
    ratio_mult   = R_mult                        ./ R0;

    subplot(numel(mics_vars), 1, v);
    hold on;
    plot(T.Time, ratio_orig,   'r-', 'LineWidth', 0.8, 'DisplayName', 'Original R/R0');
    plot(T.Time, ratio_comp1D, 'b-', 'LineWidth', 0.8, 'DisplayName', 'T-only (additive)');
    plot(T.Time, ratio_mult,   'g-', 'LineWidth', 0.8, 'DisplayName', 'T+RH (multiplicative)');
    yline(1, 'k--', 'LineWidth', 1.5, 'DisplayName', 'R/R0 = 1');
    yyaxis right
    plot(T.Time, T.chamber_actual_degC, 'k:', 'LineWidth', 0.5, 'DisplayName', 'Chamber T');
    plot(T.Time, T.humidity,            'm--','LineWidth', 0.5, 'DisplayName', 'Humidity');
    ylabel('Chamber T (°C) / Humidity (%RH)');
    yyaxis left
    ylabel(sprintf('%s / %s (-)', varname, r0_name));
    legend('Location', 'best', 'FontSize', 7);
    grid on;
    hold off;
end
xlabel('Time');

%% ── 9. STEADY-STATE FIT QUALITY CHECK ────────────────────────────────────
% Extract mean R and mean T for each steady-state plateau.
% Compare measured mean vs. model prediction to assess fit quality.

fprintf('\n── Steady-state fit quality ──\n');

% Detect individual steady-state segments (separated by ramps)
% Label each contiguous steady-state block with an ID
ss_label = zeros(height(T), 1);
seg_id   = 0;
in_ss    = false;
for k = 1:height(T)
    if idx_steady(k)
        if ~in_ss
            seg_id = seg_id + 1;
            in_ss  = true;
        end
        ss_label(k) = seg_id;
    else
        in_ss = false;
    end
end
n_segments = seg_id;
fprintf('  Detected %d steady-state segments\n', n_segments);

figure('Name', 'MICS steady-state fit quality', 'NumberTitle', 'off');
sgtitle('MICS steady-state: measured mean vs. model prediction');

for v = 1:numel(mics_vars)
    varname = mics_vars{v};
    r0_name = mics_r0{v};
    R0_val  = mean(T.(r0_name));  % R0 is constant, take mean
    y_meas  = T.(varname);
    fr      = fit_results.(varname);

    T_ss_mean   = zeros(n_segments, 1);
    R_ss_mean   = zeros(n_segments, 1);
    R_ss_pred   = zeros(n_segments, 1);
    R_ss_mult   = zeros(n_segments, 1);
    RH_ss_mean  = zeros(n_segments, 1);

    for s = 1:n_segments
        idx_s = ss_label == s;
        % Skip very short segments (< 60 samples)
        if sum(idx_s) < 60, continue; end
        T_ss_mean(s)  = mean(T_chamber(idx_s));
        R_ss_mean(s)  = mean(y_meas(idx_s));
        RH_ss_mean(s) = mean(T.humidity(idx_s));

        % T-only exponential model prediction
        R_ss_pred(s) = exp(fr.p(2)) * exp(fr.p(1) * T_ss_mean(s));

        % Multiplicative model prediction
        coeff_mult = T.([varname '_comp_mult']);  % Already computed
        % Recompute from stored coefficients via back-calculation not needed —
        % just use mean of compensated values as check
        R_ss_mult(s) = mean(T.([varname '_comp_mult'])(idx_s));
    end

    % Remove empty segments
    valid_s = T_ss_mean ~= 0;

    subplot(numel(mics_vars), 1, v);
    hold on;
    scatter(T_ss_mean(valid_s), R_ss_mean(valid_s) ./ R0_val,  60, 'r', 'filled', 'DisplayName', 'Measured mean');
    scatter(T_ss_mean(valid_s), R_ss_pred(valid_s) ./ R0_val,  60, 'b', '^',      'DisplayName', 'T-only model');
    scatter(T_ss_mean(valid_s), R_ss_mult(valid_s) ./ R0_val,  60, 'g', 's',      'DisplayName', 'T+RH mult model');
    yline(1, 'k--', 'LineWidth', 1, 'DisplayName', 'R/R0 = 1');
    xlabel('Chamber temperature (°C)');
    ylabel(sprintf('%s / R0 (-)', varname));
    legend('Location', 'best', 'FontSize', 7);
    grid on;
    hold off;
end