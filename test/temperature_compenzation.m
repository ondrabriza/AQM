clc;
clear;
close all;

% =========================================================================
% 1. INITIALIZATION & DATA LOADING
% =========================================================================

csv_file  = 'csv_data/aqm_data_2026_04_24.csv';
xlsx_file = 'CTS_temperature_chamber_data_2026_04_24_15_25.xlsx';

% The AQM device RTC was 2 hours and 13 minutes behind the chamber time
aqm_time_offset = hours(2) + minutes(13);   

% Define the precise measurement window based on chamber time
t_start = datetime(2026, 4, 24, 11, 25, 0);
t_end   = datetime(2026, 4, 24, 15, 25, 0);

% --- Load AQM Data (CSV) ---
opts = detectImportOptions(csv_file, 'Delimiter', ';');
opts.VariableNamingRule = 'preserve';
opts = setvaropts(opts, 'Timestamp', 'InputFormat', 'yyyy-MM-dd HH:mm:ss.SSS');

aqm_raw = readtimetable(csv_file, opts, 'RowTimes', 'Timestamp');
aqm_raw.Properties.DimensionNames{1} = 'Time';

% Apply time synchronization offset
aqm_raw.Time = aqm_raw.Time + aqm_time_offset;

% Trim data to the measurement window
aqm = aqm_raw(timerange(t_start, t_end, 'closed'), :);
fprintf('AQM Data loaded: %d rows (%s -> %s)\n', height(aqm), string(aqm.Time(1)), string(aqm.Time(end)));

% --- Load Temperature Chamber Data (XLSX) ---
cts_raw = readtimetable(xlsx_file, 'RowTimes', 'Timestamp');
cts_raw.Properties.DimensionNames{1} = 'Time';

% Trim data to the measurement window
cts = cts_raw(timerange(t_start, t_end, 'closed'), :);
fprintf('CTS Data loaded: %d rows (%s -> %s)\n', height(cts), string(cts.Time(1)), string(cts.Time(end)));


% =========================================================================
% 2. SENSOR RESISTANCE CALCULATION (R_s)
% =========================================================================
% Equation: Rs = R_L * (RAW_ADC / (RAW_Vcc - RAW_ADC))

% Load resistors values (Ohms)
R_L_nh3 = 120e3; 
R_L_red = 390e3;
R_L_ox  = 20e3;

% Calculate denominators (RAW_Vcc - RAW_ADC)
den_red = aqm.v3v3_raw - aqm.mics_red_raw;
den_ox  = aqm.v3v3_raw - aqm.mics_ox_raw;
den_nh3 = aqm.v3v3_raw - aqm.mics_nh3_raw;

% Prevent division by zero or negative values in case of ADC saturation
den_red(den_red <= 0) = NaN;
den_ox(den_ox <= 0)   = NaN;
den_nh3(den_nh3 <= 0) = NaN;

% Calculate exact sensor resistance (R_s)
aqm.r_red_exact = R_L_red .* (aqm.mics_red_raw ./ den_red);
aqm.r_ox_exact  = R_L_ox  .* (aqm.mics_ox_raw  ./ den_ox);
aqm.r_nh3_exact = R_L_nh3 .* (aqm.mics_nh3_raw ./ den_nh3);


% =========================================================================
% 3. TEMPERATURE COMPENSATION MODELLING (MiCS & SGX)
% =========================================================================

% Define time windows for stable temperature plateaus (last 5 mins of each)
d = datetime(2026, 4, 24);
windows = [
    d + hours(11) + minutes(50), d + hours(11) + minutes(55);  %  20 °C (Start)
    d + hours(12) + minutes(25), d + hours(12) + minutes(30);  % -10 °C
    d + hours(12) + minutes(50), d + hours(12) + minutes(55);  %   0 °C
    d + hours(13) + minutes(15), d + hours(13) + minutes(20);  %  10 °C
    d + hours(13) + minutes(45), d + hours(13) + minutes(50);  %  30 °C
    d + hours(14) + minutes(10), d + hours(14) + minutes(15);  %  40 °C
    d + hours(14) + minutes(35), d + hours(14) + minutes(40);  %  50 °C
    d + hours(15) + minutes(15), d + hours(15) + minutes(20)   %  20 °C (End)
];

num_points = size(windows, 1);
T_avg     = zeros(num_points, 1);
R_red_avg = zeros(num_points, 1);
R_ox_avg  = zeros(num_points, 1);
R_nh3_avg = zeros(num_points, 1);

% Extract and average MiCS data within each stable window (Thermodynamic equilibrium)
for i = 1:num_points
    idx = (aqm.Time >= windows(i,1)) & (aqm.Time <= windows(i,2));
    
    T_avg(i)     = mean(aqm.temperature(idx), 'omitnan');
    R_red_avg(i) = mean(aqm.r_red_exact(idx), 'omitnan');
    R_ox_avg(i)  = mean(aqm.r_ox_exact(idx),  'omitnan');
    R_nh3_avg(i) = mean(aqm.r_nh3_exact(idx), 'omitnan');
end

% Hardware limit filtering: Exclude saturated points for MiCS (> 2.5 MOhm)
MAX_VALID_RESISTANCE = 2.5e6; 
valid_red = R_red_avg < MAX_VALID_RESISTANCE;
valid_ox  = R_ox_avg  < MAX_VALID_RESISTANCE;
valid_nh3 = R_nh3_avg < MAX_VALID_RESISTANCE;

% Polynomial regression (3rd degree) for MiCS sensors (Trained on stable windows)
degree_mics = 3;
p_red = polyfit(T_avg(valid_red), R_red_avg(valid_red), degree_mics);
p_ox  = polyfit(T_avg(valid_ox),  R_ox_avg(valid_ox),   degree_mics);
p_nh3 = polyfit(T_avg(valid_nh3), R_nh3_avg(valid_nh3), degree_mics);

% Polynomial regression (4th degree) for SGX sensors (Trained on FULL dataset for dynamics)
degree_sgx = 2;
p_so2 = polyfit(aqm.temperature, aqm.so2_ppm, degree_sgx);
p_h2s = polyfit(aqm.temperature, aqm.h2s_ppm, degree_sgx);

% --- PRINT COEFFICIENTS TO CONSOLE FOR C/C++ FIRMWARE ---
fprintf('\n--- Firmware Coefficients (MiCS: R = a*T^3 + b*T^2 + c*T + d) ---\n');
fprintf('RED: a = %e, b = %e, c = %e, d = %e\n', p_red(1), p_red(2), p_red(3), p_red(4));
fprintf('OX:  a = %e, b = %e, c = %e, d = %e\n', p_ox(1), p_ox(2), p_ox(3), p_ox(4));
fprintf('NH3: a = %e, b = %e, c = %e, d = %e\n', p_nh3(1), p_nh3(2), p_nh3(3), p_nh3(4));

%fprintf('\n--- Firmware Coefficients (SGX: ppm = a*T^4 + b*T^3 + c*T^2 + d*T + e) ---\n');
%fprintf('SO2: a = %e, b = %e, c = %e, d = %e, e = %e\n', p_so2(1), p_so2(2), p_so2(3), p_so2(4), p_so2(5));
%fprintf('H2S: a = %e, b = %e, c = %e, d = %e, e = %e\n\n', p_h2s(1), p_h2s(2), p_h2s(3), p_h2s(4), p_h2s(5));

fprintf('\n--- Firmware Coefficients (SGX: ppm = a*T^2 + b*T + c) ---\n');
fprintf('SO2: a = %e, b = %e, c = %e\n', p_so2(1), p_so2(2), p_so2(3));
fprintf('H2S: a = %e, b = %e, c = %e\n', p_h2s(1), p_h2s(2), p_h2s(3));

% Generate smooth curves for plotting the MiCS regression models
T_fit = linspace(min(T_avg)-5, max(T_avg)+5, 100);
R_red_fit = polyval(p_red, T_fit);
R_ox_fit  = polyval(p_ox,  T_fit);
R_nh3_fit = polyval(p_nh3, T_fit);


% =========================================================================
% 4. APLIKACE KOMPENZACE NA CELÝ DATOVÝ SOUBOR (Zlepšený DSP přístup)
% =========================================================================

% Referenční teplota (20 °C) a základní hodnoty senzorů
T_ref = 20.0;
R_ref_red = polyval(p_red, T_ref);
R_ref_ox  = polyval(p_ox,  T_ref);
R_ref_nh3 = polyval(p_nh3, T_ref);
SO2_ref   = polyval(p_so2, T_ref);
H2S_ref   = polyval(p_h2s, T_ref);

% --- KROK 1: ODSTRANĚNÍ ŠUMU ZE SUROVÝCH DAT ---
% Všechny vstupní veličiny vyhladíme PŘED jakoukoliv matematikou
window_size = 90; 
aqm.temp_smooth = movmean(aqm.temperature, window_size);
aqm.so2_raw_smooth = movmean(aqm.so2_ppm, window_size);
aqm.h2s_raw_smooth = movmean(aqm.h2s_ppm, window_size);

% Můžeme rovnou vyhladit i odpory pro čistší výsledek u MiCS
aqm.r_red_exact_smooth = movmean(aqm.r_red_exact, window_size);
aqm.r_ox_exact_smooth  = movmean(aqm.r_ox_exact, window_size);
aqm.r_nh3_exact_smooth = movmean(aqm.r_nh3_exact, window_size);


% --- KROK 2: MiCS Senzory (Multiplikativní kompenzace z vyhlazených dat) ---
% Kompenzační faktor počítáme z vyhlazené teploty
CF_red = polyval(p_red, aqm.temp_smooth) ./ R_ref_red;
CF_ox  = polyval(p_ox,  aqm.temp_smooth) ./ R_ref_ox;
CF_nh3 = polyval(p_nh3, aqm.temp_smooth) ./ R_ref_nh3;

% Zkompenzované hodnoty počítáme z vyhlazeného odporu a vyhlazeného CF
aqm.r_red_comp = aqm.r_red_exact_smooth ./ CF_red;
aqm.r_ox_comp  = aqm.r_ox_exact_smooth  ./ CF_ox;
aqm.r_nh3_comp = aqm.r_nh3_exact_smooth ./ CF_nh3;


% --- KROK 3: SGX Senzory (Aditivní kompenzace z vyhlazených dat) ---
% Vypočítáme teplotní drift pomocí vyhlazené teploty
drift_so2 = polyval(p_so2, aqm.temp_smooth) - SO2_ref;
drift_h2s = polyval(p_h2s, aqm.temp_smooth) - H2S_ref;

% Odečteme hladký drift od hladkých surových dat a OŘÍZNEME na nulu
aqm.so2_comp_smooth = max(aqm.so2_raw_smooth - drift_so2, 0);
aqm.h2s_comp_smooth = max(aqm.h2s_raw_smooth - drift_h2s, 0);

% Proměnné pro Graf 6, aby to fungovalo s novými jmény z Kroku 1
aqm.so2_ppm_smooth = aqm.so2_raw_smooth; 
aqm.h2s_ppm_smooth = aqm.h2s_raw_smooth;

% =========================================================================
% 5. PLOTTING
% =========================================================================

%% FIGURE 1: Original time series (MiCS Uncompensated)
figure(1)
set(gcf, 'Name', 'MiCS - Nekompenzovane v case', 'Position', [100, 500, 1000, 400]);

yyaxis left
p1 = plot(aqm.Time, aqm.temperature, 'LineWidth', 1.5, 'DisplayName', 'SEN55 Teplota'); hold on;
p2 = plot(cts.Time, cts.Actual_C, 'LineWidth', 1.5, 'DisplayName', 'Komora (Skutečnost)');
p3 = plot(cts.Time, cts.Target_C, '--', 'Color', [0.5 0.5 0.5], 'DisplayName', 'Komora (Cíl)');
ylabel('Teplota [°C]');

yyaxis right
p4 = plot(aqm.Time, aqm.r_red_exact, 'LineWidth', 1, 'DisplayName', 'RED');
p5 = plot(aqm.Time, aqm.r_ox_exact, 'LineWidth', 1, 'DisplayName', 'OX');
p6 = plot(aqm.Time, aqm.r_nh3_exact, 'LineWidth', 1, 'DisplayName', 'NH3');
ylabel('Odpor senzoru [\Omega]');

title('Průběh teplot a nekompenzovaných odporů (MiCS)');
legend([p1, p2, p3, p4, p5, p6], 'Location', 'best');
grid on;


%% FIGURE 2: Regression Models (MiCS)
figure(2)
set(gcf, 'Name', 'MiCS - Teplotni model', 'Position', [150, 50, 1000, 350]);

subplot(1,3,1);
scatter(T_avg(valid_red), R_red_avg(valid_red), 60, 'b', 'filled'); hold on;
scatter(T_avg(~valid_red), R_red_avg(~valid_red), 60, 'r', 'x', 'LineWidth', 1.5);
plot(T_fit, R_red_fit, 'g-', 'LineWidth', 2);
title('RED (CO, VOC)'); xlabel('Teplota SEN55 [°C]'); ylabel('Odpor [\Omega]'); grid on;

subplot(1,3,2);
scatter(T_avg(valid_ox), R_ox_avg(valid_ox), 60, 'b', 'filled'); hold on;
scatter(T_avg(~valid_ox), R_ox_avg(~valid_ox), 60, 'r', 'x', 'LineWidth', 1.5);
plot(T_fit, R_ox_fit, 'g-', 'LineWidth', 2);
title('OX (NO_2)'); xlabel('Teplota SEN55 [°C]'); grid on;
legend('Validní data', 'Saturováno (>2.5M\Omega)', 'Regresní model', 'Location', 'best');

subplot(1,3,3);
scatter(T_avg(valid_nh3), R_nh3_avg(valid_nh3), 60, 'b', 'filled'); hold on;
scatter(T_avg(~valid_nh3), R_nh3_avg(~valid_nh3), 60, 'r', 'x', 'LineWidth', 1.5);
plot(T_fit, R_nh3_fit, 'g-', 'LineWidth', 2);
title('NH3'); xlabel('Teplota SEN55 [°C]'); grid on;


%% FIGURE 3: Comparison Subplots (MiCS Uncompensated vs Compensated)
figure(3)
set(gcf, 'Name', 'MiCS - Porovnani kompenzace', 'Position', [200, 150, 1200, 400]);

subplot(1,3,1);
plot(aqm.Time, aqm.temperature, '-', 'Color', [0.8 0.8 0.8]); yyaxis right; 
plot(aqm.Time, aqm.r_red_exact, 'r-', 'LineWidth', 1); hold on;
plot(aqm.Time, aqm.r_red_comp, 'b-', 'LineWidth', 2);
title('RED: Nekompenzovaný vs. Kompenzovaný'); ylabel('Odpor [\Omega]');
legend('Teplota', 'Nekompenzovaný R', 'Kompenzovaný R', 'Location', 'best'); grid on;

subplot(1,3,2);
plot(aqm.Time, aqm.temperature, '-', 'Color', [0.8 0.8 0.8]); yyaxis right;
plot(aqm.Time, aqm.r_ox_exact, 'r-', 'LineWidth', 1); hold on;
plot(aqm.Time, aqm.r_ox_comp, 'b-', 'LineWidth', 2);
title('OX: Nekompenzovaný vs. Kompenzovaný'); grid on;

subplot(1,3,3);
plot(aqm.Time, aqm.temperature, '-', 'Color', [0.8 0.8 0.8]); yyaxis right;
plot(aqm.Time, aqm.r_nh3_exact, 'r-', 'LineWidth', 1); hold on;
plot(aqm.Time, aqm.r_nh3_comp, 'b-', 'LineWidth', 2);
title('NH3: Nekompenzovaný vs. Kompenzovaný'); grid on;


%% FIGURE 4: Time series (MiCS Compensated)
figure(4)
set(gcf, 'Name', 'MiCS - Kompenzovane v case', 'Position', [250, 100, 1000, 400]);

yyaxis left
p1 = plot(aqm.Time, aqm.temperature, 'LineWidth', 1.5, 'DisplayName', 'SEN55 Teplota'); hold on;
p2 = plot(cts.Time, cts.Actual_C, 'LineWidth', 1.5, 'DisplayName', 'Komora (Skutečnost)');
p3 = plot(cts.Time, cts.Target_C, '--', 'Color', [0.5 0.5 0.5], 'DisplayName', 'Komora (Cíl)');
ylabel('Teplota [°C]');

yyaxis right
p4 = plot(aqm.Time, aqm.r_red_comp, 'LineWidth', 1, 'DisplayName', 'RED');
p5 = plot(aqm.Time, aqm.r_ox_comp, 'LineWidth', 1, 'DisplayName', 'OX');
p6 = plot(aqm.Time, aqm.r_nh3_comp, 'LineWidth', 1, 'DisplayName', 'NH3');
ylabel('Odpor senzoru [\Omega]');

title('Průběh teplot a kompenzovaných odporů (MiCS)');
legend([p1, p2, p3, p4, p5, p6], 'Location', 'best');
grid on;


%% FIGURE 5: SGX & SEN55 (Raw Time Series)
figure(5)
set(gcf, 'Name', 'Ostatni senzory v case', 'Position', [300, 150, 1200, 600]);

subplot(2,2,1);
plot(aqm.Time, aqm.temperature, '-', 'Color', [0.8 0.8 0.8]); yyaxis right;
plot(aqm.Time, aqm.voc_index, 'm-', 'LineWidth', 1.5);
title('SEN55: VOC Index'); ylabel('Index'); 
legend('Teplota', 'VOC Index', 'Location', 'best'); grid on;

subplot(2,2,2);
plot(aqm.Time, aqm.temperature, '-', 'Color', [0.8 0.8 0.8]); yyaxis right;
plot(aqm.Time, aqm.nox_index, 'c-', 'LineWidth', 1.5);
title('SEN55: NOx Index'); ylabel('Index');
legend('Teplota', 'NOx Index', 'Location', 'best'); grid on;

subplot(2,2,3);
plot(aqm.Time, aqm.temperature, '-', 'Color', [0.8 0.8 0.8]); yyaxis right;
plot(aqm.Time, aqm.so2_ppm, 'g-', 'LineWidth', 1.5);
title('SGX: Koncentrace SO_2'); ylabel('Koncentrace [ppm]');
legend('Teplota', 'SO_2 (Nekompenzované)', 'Location', 'best'); grid on;

subplot(2,2,4);
plot(aqm.Time, aqm.temperature, '-', 'Color', [0.8 0.8 0.8]); yyaxis right;
plot(aqm.Time, aqm.h2s_ppm, 'y-', 'LineWidth', 1.5);
title('SGX: Koncentrace H_2S'); ylabel('Koncentrace [ppm]');
legend('Teplota', 'H_2S (Nekompenzované)', 'Location', 'best'); grid on;


%% FIGURE 6: SGX Compensation Comparison (Smoothed & Clamped)
figure(6)
set(gcf, 'Name', 'SGX - Porovnani kompenzace', 'Position', [350, 200, 1000, 450]);

subplot(1,2,1);
plot(aqm.Time, aqm.temperature, '-', 'Color', [0.8 0.8 0.8]); yyaxis right;
plot(aqm.Time, aqm.so2_ppm_smooth, 'r-', 'LineWidth', 1); hold on;
plot(aqm.Time, aqm.so2_comp_smooth, 'b-', 'LineWidth', 2);
title('SO_2: Nekompenzovaný vs. Kompenzovaný'); ylabel('Koncentrace [ppm]');
legend('Teplota', 'Nekompenzované ppm', 'Kompenzované ppm', 'Location', 'best'); grid on;

subplot(1,2,2);
plot(aqm.Time, aqm.temperature, '-', 'Color', [0.8 0.8 0.8]); yyaxis right;
plot(aqm.Time, aqm.h2s_ppm_smooth, 'r-', 'LineWidth', 1); hold on;
plot(aqm.Time, aqm.h2s_comp_smooth, 'b-', 'LineWidth', 2);
title('H_2S: Nekompenzovaný vs. Kompenzovaný'); ylabel('Koncentrace [ppm]');
legend('Teplota', 'Nekompenzované ppm', 'Kompenzované ppm', 'Location', 'best'); grid on;

% =========================================================================
% 6. STATISTICKÁ ANALÝZA: ZÁVISLOST NA TEPLOTĚ (PŘED A PO KOMPENZACI)
% =========================================================================

fprintf('\n======================================================\n');
fprintf('   ZÁVISLOST NA TEPLOTĚ: PŘED KOMPENZACÍ vs. PO NÍ\n');
fprintf('   (Korelační koeficient r: 0 = imunní, 1/-1 = závislé)\n');
fprintf('======================================================\n');

% Pomocná funkce pro bezpečný výpočet Pearsonovy korelace (vynechá NaN hodnoty)
calc_corr = @(x, y) corrcoef(x, y, 'Rows', 'complete');

% --- SGX SO2 ---
c_so2_raw  = calc_corr(aqm.temp_smooth, aqm.so2_raw_smooth); 
c_so2_comp = calc_corr(aqm.temp_smooth, aqm.so2_comp_smooth); 
fprintf('SGX SO2:\n  Před: r = %6.3f \n  Po:   r = %6.3f\n\n', c_so2_raw(1,2), c_so2_comp(1,2));

% --- SGX H2S ---
c_h2s_raw  = calc_corr(aqm.temp_smooth, aqm.h2s_raw_smooth); 
c_h2s_comp = calc_corr(aqm.temp_smooth, aqm.h2s_comp_smooth); 
fprintf('SGX H2S:\n  Před: r = %6.3f \n  Po:   r = %6.3f\n\n', c_h2s_raw(1,2), c_h2s_comp(1,2));

% --- MiCS Senzory ---
% Pro spravedlivý výpočet vyřadíme body, kde byl senzor v mrazu saturován (> 2.5 MOhm)
idx_red = aqm.r_red_exact_smooth < MAX_VALID_RESISTANCE;
idx_ox  = aqm.r_ox_exact_smooth < MAX_VALID_RESISTANCE;
idx_nh3 = aqm.r_nh3_exact_smooth < MAX_VALID_RESISTANCE;

c_red_raw  = calc_corr(aqm.temp_smooth(idx_red), aqm.r_red_exact_smooth(idx_red)); 
c_red_comp = calc_corr(aqm.temp_smooth(idx_red), aqm.r_red_comp(idx_red)); 
fprintf('MiCS RED (VOC, CO):\n  Před: r = %6.3f \n  Po:   r = %6.3f\n\n', c_red_raw(1,2), c_red_comp(1,2));

c_ox_raw  = calc_corr(aqm.temp_smooth(idx_ox), aqm.r_ox_exact_smooth(idx_ox)); 
c_ox_comp = calc_corr(aqm.temp_smooth(idx_ox), aqm.r_ox_comp(idx_ox)); 
fprintf('MiCS OX (NO2):\n  Před: r = %6.3f \n  Po:   r = %6.3f\n\n', c_ox_raw(1,2), c_ox_comp(1,2));

c_nh3_raw  = calc_corr(aqm.temp_smooth(idx_nh3), aqm.r_nh3_exact_smooth(idx_nh3)); 
c_nh3_comp = calc_corr(aqm.temp_smooth(idx_nh3), aqm.r_nh3_comp(idx_nh3)); 
fprintf('MiCS NH3:\n  Před: r = %6.3f \n  Po:   r = %6.3f\n', c_nh3_raw(1,2), c_nh3_comp(1,2));
fprintf('======================================================\n');

%%
% === PŘÍPRAVA DAT (Zůstává stejná) ===
step = 10; % Pro přehlednost vykreslíme každý 10. bod
T_plt = aqm.temp_smooth(1:step:end);
sz = 20;   % Velikost značek
alpha = 0.5;

% =========================================================================
% FIGURE 7a: SGX Senzory (SO2 a H2S)
% =========================================================================
figure(71) % Nové číslo pro SGX
set(gcf, 'Name', 'Zavislost SGX na teplote', 'Position', [100, 100, 1200, 500]);

% 1. SGX SO2
subplot(1,2,1); % 1 řádek, 2 sloupce, 1. graf
scatter(T_plt, aqm.so2_raw_smooth(1:step:end), sz, 'r', '+'); hold on;
scatter(T_plt, aqm.so2_comp_smooth(1:step:end), sz, 'b', 'o', 'filled', 'MarkerFaceAlpha', alpha);
title(sprintf('SGX SO_2\nPřed: r = %.2f | Po: r = %.2f', c_so2_raw(1,2), c_so2_comp(1,2)));
xlabel('Teplota SEN55 [°C]'); ylabel('Koncentrace [ppm]');
grid on;
legend('Nekompenzovaná data', 'Kompenzovaná data', 'Location', 'northeast');

% 2. SGX H2S
subplot(1,2,2); % 1 řádek, 2 sloupce, 2. graf
scatter(T_plt, aqm.h2s_raw_smooth(1:step:end), sz, 'r', '+'); hold on;
scatter(T_plt, aqm.h2s_comp_smooth(1:step:end), sz, 'b', 'o', 'filled', 'MarkerFaceAlpha', alpha);
title(sprintf('SGX H_2S\nPřed: r = %.2f | Po: r = %.2f', c_h2s_raw(1,2), c_h2s_comp(1,2)));
xlabel('Teplota SEN55 [°C]'); ylabel('Koncentrace [ppm]');
grid on;

% =========================================================================
% FIGURE 7b: MiCS Senzory (RED, OX, NH3)
% =========================================================================
figure(72) % Nové číslo pro MiCS
set(gcf, 'Name', 'Zavislost MiCS na teplote', 'Position', [100, 100, 1200, 400]);

% 1. MiCS RED
subplot(1,3,1); % 1 řádek, 3 sloupce, 1. graf
scatter(T_plt, aqm.r_red_exact_smooth(1:step:end), sz, 'r', '+'); hold on;
scatter(T_plt, aqm.r_red_comp(1:step:end), sz, 'b', 'o', 'filled', 'MarkerFaceAlpha', alpha);
title(sprintf('MiCS RED\nPřed: r = %.2f | Po: r = %.2f', c_red_raw(1,2), c_red_comp(1,2)));
xlabel('Teplota SEN55 [°C]'); ylabel('Odpor [\Omega]'); 
grid on;
legend('Nekompenzovaná data', 'Kompenzovaná data', 'Location', 'northeast');

% 2. MiCS OX
subplot(1,3,2); % 1 řádek, 3 sloupce, 2. graf
scatter(T_plt, aqm.r_ox_exact_smooth(1:step:end), sz, 'r', '+'); hold on;
scatter(T_plt, aqm.r_ox_comp(1:step:end), sz, 'b', 'o', 'filled', 'MarkerFaceAlpha', alpha);
title(sprintf('MiCS OX\nPřed: r = %.2f | Po: r = %.2f', c_ox_raw(1,2), c_ox_comp(1,2)));
xlabel('Teplota SEN55 [°C]'); ylabel('Odpor [\Omega]'); 
grid on;

% 3. MiCS NH3
subplot(1,3,3); % 1 řádek, 3 sloupce, 3. graf
scatter(T_plt, aqm.r_nh3_exact_smooth(1:step:end), sz, 'r', '+'); hold on;
scatter(T_plt, aqm.r_nh3_comp(1:step:end), sz, 'b', 'o', 'filled', 'MarkerFaceAlpha', alpha);
title(sprintf('MiCS NH_3\nPřed: r = %.2f | Po: r = %.2f', c_nh3_raw(1,2), c_nh3_comp(1,2)));
xlabel('Teplota SEN55 [°C]'); ylabel('Odpor [\Omega]'); 
grid on;

%% FIGURE 8: Průběh teplot (Relativní čas v minutách)
figure(8)
set(gcf, 'Name', 'Průběh teplot v teplotní komoře a modulu během testu');

% 1. Najdeme úplný začátek testu (nejmenší čas z obou měření)
t_start = min(aqm.Time(1), cts.Time(1));

% 2. Přepočítáme oba časy na minuty od začátku testu
% (Předpokládá, že aqm.Time a cts.Time jsou typu 'datetime')
aqm_time_min = minutes(aqm.Time - t_start);
cts_time_min = minutes(cts.Time - t_start);

% 3. Vykreslení křivek s novými časovými osami
p1 = plot(aqm_time_min, aqm.temperature, ':','LineWidth', 1.5, 'DisplayName', 'SEN55'); 
hold on;
p2 = plot(cts_time_min, cts.Actual_C, 'LineWidth', 1.5, 'DisplayName', 'Komora (Skutečnost)');
p3 = plot(cts_time_min, cts.Target_C, '--', 'Color', 'k','LineWidth', 1, 'DisplayName', 'Komora (Cíl)');

% 4. Popisky os (title záměrně chybí kvůli exportu do LaTeXu)
ylabel('Teplota [°C]');
xlabel('Čas testu [min]');

title('Průběh teplot v teplotní komoře')
legend([p1, p2, p3], 'Location', 'best');
grid on;

max_time_min = max(max(aqm_time_min), max(cts_time_min));
xlim([0, max_time_min]);

% 6. VÝZNAMNÉ BODY NA OSE X (podle tabulky 6.1)
%vyznamne_body = [0, 30, 45, 65, 70, 90, 95, 115, 125, 145, 150, 170, 175, 195, 210, 240];
%xticks(vyznamne_body);
%xtickangle(45); % Natočí čísla o 45 stupňů, aby se nepřekrývala
%xline(vyznamne_body, ':', 'Color', [0.8 0.8 0.8], 'HandleVisibility', 'off');


hold off;
