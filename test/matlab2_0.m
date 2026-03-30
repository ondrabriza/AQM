clc
clear 
close all

%% 0. NASTAVENÍ ČASOVÉHO FILTRU
enable_time_filter = true;  % Změň na 'false', pokud chceš analyzovat celý soubor
filter_start = '24.03.2026 12:00:00';
filter_end   = '25.03.2026 8:00:00';

%% 1. IMPORT DAT A PŘÍPRAVA ČASOVÉ OSY
filename = 'data24_3.csv'; 
opts = detectImportOptions(filename);
opts.VariableNamingRule = 'preserve'; 
data = readtable(filename, opts);

% Zajištění správného formátu času (pro osu X)
if iscell(data.Timestamp) || isstring(data.Timestamp)
    data.Timestamp = datetime(data.Timestamp);
end

% Aplikace časového filtru (pokud je zapnutý)
if enable_time_filter
    % Převod textového zadání na datetime
    t_start = datetime(filter_start, 'InputFormat', 'dd.MM.yyyy HH:mm:ss');
    t_end   = datetime(filter_end, 'InputFormat', 'dd.MM.yyyy HH:mm:ss');
    
    % Vytvoření masky a oříznutí tabulky
    time_mask = (data.Timestamp >= t_start) & (data.Timestamp <= t_end);
    data = data(time_mask, :);
    
    fprintf('--- Časový filtr AKTIVNÍ: %s až %s (Nalezeno %d řádků) ---\n', filter_start, filter_end, height(data));
else
    fprintf('--- Časový filtr VYPNUTÝ: Zpracovávám všechna data (%d řádků) ---\n', height(data));
end

% Seřazení oříznutých dat čistě podle času
clean_data = sortrows(data, 'Timestamp');

% Ochrana proti prázdnému výběru
if height(clean_data) == 0
    error('CHYBA: V zadaném časovém rozmezí nejsou žádná data. Zkontroluj časy ve filtru!');
end

%% 2. KROK 1: GATING (Odstranění glitchů v napájení)
V5V_threshold = 4.9; 
V3V3_threshold = 3.3;
power_mask = (clean_data.V5V >= V5V_threshold) & (clean_data.V3V3 >= V3V3_threshold);

% Vytvoříme 'raw_data' (surová data s NaN výpadky)
raw_data = clean_data;
var_names = raw_data.Properties.VariableNames;
for i = 1:length(var_names)
    if ~strcmp(var_names{i}, 'Uptime') && ~strcmp(var_names{i}, 'Timestamp')
        raw_data.(var_names{i})(~power_mask) = NaN;
    end
end

% 'final_data' (kompenzovaná data)
final_data = raw_data;

%% 3. KROK 2: VÍCENÁSOBNÁ KOMPENZACE (VLHKOST + TEPLOTA)
vars_to_fix = {'SO2_ppm', 'H2S_ppm', 'R_CO_kOhm', 'R_NH3_kOhm', 'R_NO2_kOhm', 'VOC', 'NOx'};
fprintf('--- Start vícenásobné kompenzace (T + RH) ---\n');

x_hum = raw_data.Humidity_pct;
x_temp = raw_data.Temp_C;

for i = 1:length(vars_to_fix)
    var_name = vars_to_fix{i};
    y_raw = raw_data.(var_name);
    
    valid_idx = ~isnan(y_raw) & ~isnan(x_hum) & ~isnan(x_temp);
    
    if sum(valid_idx) > 10 && std(y_raw(valid_idx)) > 0
        X_matrix = [ones(sum(valid_idx), 1), x_hum(valid_idx), x_temp(valid_idx)];
        Y_vector = y_raw(valid_idx);
        beta = X_matrix \ Y_vector; 
        
        y_model = beta(1) + beta(2) * x_hum + beta(3) * x_temp;
        y_final = (y_raw - y_model) + mean(y_raw(valid_idx));
        
        final_data.(var_name) = y_final;
        fprintf('Senzor %s: Kompenzováno (RH: %.4f, Temp: %.4f)\n', var_name, beta(2), beta(3));
    else
        fprintf('Senzor %s: Přeskočeno (konstantní/chybí data)\n', var_name);
    end
end
fprintf('--- Zahozeno %.2f %% dat kvůli glitchům. ---\n', (sum(~power_mask)/length(power_mask))*100);

% Styl vykreslování
c1 = [0.4940 0.1840 0.5560]; c2 = [0.9290 0.6940 0.1250]; c3 = [0.3010 0.7450 0.9330];

%% ========================================================================
%% FIGURE 1: TILED LAYOUT - POUZE SUROVÁ DATA
%% ========================================================================
figure(1); set(gcf, 'Name', 'Fig 1: Tiled - Surová data', 'Units', 'normalized', 'Position', [0.02, 0.05, 0.3, 0.85]);
t1 = tiledlayout(6, 1, 'TileSpacing', 'compact', 'Padding', 'compact');
title(t1, 'Nekompenzovaná (Surová) data', 'FontSize', 12, 'FontWeight', 'bold');

ax1 = nexttile; yyaxis(ax1, 'left'); plot(raw_data.Timestamp, raw_data.Temp_C, 'r.-'); ylabel('Temp [°C]');
yyaxis(ax1, 'right'); plot(raw_data.Timestamp, raw_data.Humidity_pct, 'b.-'); ylabel('Hum [%]'); grid on;
ax2 = nexttile; plot(raw_data.Timestamp, raw_data.SO2_ppm, '.-'); hold on; plot(raw_data.Timestamp, raw_data.H2S_ppm, '.-'); legend('SO2', 'H2S','Location','best'); grid on;
ax3 = nexttile; plot(raw_data.Timestamp, raw_data.R_CO_kOhm, '.-'); hold on; plot(raw_data.Timestamp, raw_data.R_NH3_kOhm, '.-'); plot(raw_data.Timestamp, raw_data.R_NO2_kOhm, '.-'); legend('R_{CO}', 'R_{NH3}', 'R_{NO2}','Location','best'); grid on;
ax4 = nexttile; plot(raw_data.Timestamp, raw_data.PM25, '.-'); hold on; plot(raw_data.Timestamp, raw_data.PM10, '.-'); legend('PM2.5', 'PM10.0','Location','best'); grid on;
ax5 = nexttile; plot(raw_data.Timestamp, raw_data.VOC, 'm.-'); hold on; plot(raw_data.Timestamp, raw_data.NOx, 'c.-'); legend('VOC', 'NOx','Location','best'); grid on;
ax6 = nexttile; plot(raw_data.Timestamp, raw_data.V3V3, 'g.-'); hold on; plot(raw_data.Timestamp, raw_data.V5V, 'k.-'); legend('3.3V', '5.0V','Location','best'); grid on;
linkaxes([ax1 ax2 ax3 ax4 ax5 ax6], 'x'); xtickformat(ax6, 'HH:mm');

%% ========================================================================
%% FIGURE 2: TILED LAYOUT - POUZE KOMPENZOVANÁ DATA
%% ========================================================================
figure(2); set(gcf, 'Name', 'Fig 2: Tiled - Kompenzovaná data', 'Units', 'normalized', 'Position', [0.33, 0.05, 0.3, 0.85]);
t2 = tiledlayout(6, 1, 'TileSpacing', 'compact', 'Padding', 'compact');
title(t2, 'Kompenzovaná data', 'FontSize', 12, 'FontWeight', 'bold');

ax1 = nexttile; yyaxis(ax1, 'left'); plot(final_data.Timestamp, final_data.Temp_C, 'r.-'); ylabel('Temp [°C]');
yyaxis(ax1, 'right'); plot(final_data.Timestamp, final_data.Humidity_pct, 'b.-'); ylabel('Hum [%]'); grid on;
ax2 = nexttile; plot(final_data.Timestamp, final_data.SO2_ppm, '.-'); hold on; plot(final_data.Timestamp, final_data.H2S_ppm, '.-'); legend('SO2', 'H2S','Location','best'); grid on;
ax3 = nexttile; plot(final_data.Timestamp, final_data.R_CO_kOhm, '.-'); hold on; plot(final_data.Timestamp, final_data.R_NH3_kOhm, '.-'); plot(final_data.Timestamp, final_data.R_NO2_kOhm, '.-'); legend('R_{CO}', 'R_{NH3}', 'R_{NO2}','Location','best'); grid on;
ax4 = nexttile; plot(final_data.Timestamp, final_data.PM25, '.-'); hold on; plot(final_data.Timestamp, final_data.PM10, '.-'); legend('PM2.5', 'PM10.0','Location','best'); grid on;
ax5 = nexttile; plot(final_data.Timestamp, final_data.VOC, 'm.-'); hold on; plot(final_data.Timestamp, final_data.NOx, 'c.-'); legend('VOC', 'NOx','Location','best'); grid on;
ax6 = nexttile; plot(final_data.Timestamp, final_data.V3V3, 'g.-'); hold on; plot(final_data.Timestamp, final_data.V5V, 'k.-'); legend('3.3V', '5.0V','Location','best'); grid on;
linkaxes([ax1 ax2 ax3 ax4 ax5 ax6], 'x'); xtickformat(ax6, 'HH:mm');

%% ========================================================================
%% FIGURE 3: TILED LAYOUT - PŘEKRYTÍ (OVERLAY)
%% ========================================================================
figure(3); set(gcf, 'Name', 'Fig 3: Tiled - Překrytí obou vrstev', 'Units', 'normalized', 'Position', [0.65, 0.05, 0.33, 0.85]);
t3 = tiledlayout(6, 1, 'TileSpacing', 'compact', 'Padding', 'compact');
title(t3, 'Překrytí (Čárkovaně=Surová, Plně=Komp.)', 'FontSize', 12, 'FontWeight', 'bold');

ax1 = nexttile; yyaxis(ax1, 'left'); plot(raw_data.Timestamp, raw_data.Temp_C, 'r--'); hold on; plot(final_data.Timestamp, final_data.Temp_C, 'r.-'); ylabel('Temp');
yyaxis(ax1, 'right'); plot(raw_data.Timestamp, raw_data.Humidity_pct, 'b--'); hold on; plot(final_data.Timestamp, final_data.Humidity_pct, 'b.-'); ylabel('Hum'); grid on;
ax2 = nexttile; plot(raw_data.Timestamp, raw_data.SO2_ppm, '--'); hold on; plot(final_data.Timestamp, final_data.SO2_ppm, '.-'); plot(raw_data.Timestamp, raw_data.H2S_ppm, '--'); plot(final_data.Timestamp, final_data.H2S_ppm, '.-'); legend('SO2 raw', 'SO2 komp', 'H2S raw', 'H2S komp','Location','best'); grid on;
ax3 = nexttile; plot(raw_data.Timestamp, raw_data.R_CO_kOhm, '--','Color',c1); hold on; plot(final_data.Timestamp, final_data.R_CO_kOhm, '.-','Color',c1); plot(raw_data.Timestamp, raw_data.R_NH3_kOhm, '--','Color',c2); plot(final_data.Timestamp, final_data.R_NH3_kOhm, '.-','Color',c2); plot(raw_data.Timestamp, raw_data.R_NO2_kOhm, '--','Color',c3); plot(final_data.Timestamp, final_data.R_NO2_kOhm, '.-','Color',c3); ylabel('Odpor'); grid on;
ax4 = nexttile; plot(raw_data.Timestamp, raw_data.PM25, '.-'); hold on; plot(raw_data.Timestamp, raw_data.PM10, '.-'); legend('PM2.5 (Nekomp.)', 'PM10.0 (Nekomp.)','Location','best'); grid on;
ax5 = nexttile; plot(raw_data.Timestamp, raw_data.VOC, 'm--'); hold on; plot(final_data.Timestamp, final_data.VOC, 'm.-'); plot(raw_data.Timestamp, raw_data.NOx, 'c--'); plot(final_data.Timestamp, final_data.NOx, 'c.-'); legend('VOC raw', 'VOC komp', 'NOx raw', 'NOx komp','Location','best'); grid on;
ax6 = nexttile; plot(raw_data.Timestamp, raw_data.V3V3, 'g.-'); hold on; plot(raw_data.Timestamp, raw_data.V5V, 'k.-'); grid on;
linkaxes([ax1 ax2 ax3 ax4 ax5 ax6], 'x'); xtickformat(ax6, 'HH:mm');

%% ========================================================================
%% FIGURE 4 & 5: STACKED PLOT - SEPARÁTNĚ
%% ========================================================================
vars_to_stack = {'Temp_C', 'Humidity_pct', 'SO2_ppm', 'H2S_ppm', 'R_CO_kOhm', 'R_NH3_kOhm', 'R_NO2_kOhm', 'PM25', 'VOC', 'NOx', 'V3V3', 'V5V'};

figure(4); set(gcf, 'Name', 'Fig 4: Stacked - Surová data', 'Units', 'normalized', 'Position', [0.05, 0.1, 0.4, 0.8]);
s4 = stackedplot(raw_data, vars_to_stack, 'XVariable', 'Timestamp');
title('Fig 4: Stacked Plot - Surová data');

figure(5); set(gcf, 'Name', 'Fig 5: Stacked - Kompenzovaná data', 'Units', 'normalized', 'Position', [0.5, 0.1, 0.4, 0.8]);
s5 = stackedplot(final_data, vars_to_stack, 'XVariable', 'Timestamp');
title('Fig 5: Stacked Plot - Kompenzovaná data');

%% ========================================================================
%% FIGURE 6: STACKED PLOT - PŘEKRYTÍ (OVERLAY)
%% ========================================================================
overlay_tbl = table();
overlay_tbl.Timestamp = raw_data.Timestamp;

stacked_overlay_vars = {};
for i = 1:length(vars_to_stack)
    v = vars_to_stack{i};
    overlay_tbl.([v '_raw']) = raw_data.(v);
    overlay_tbl.([v '_comp']) = final_data.(v);
    stacked_overlay_vars{end+1} = {[v '_raw'], [v '_comp']};
end

figure(6); set(gcf, 'Name', 'Fig 6: Stacked - Překrytí obou vrstev', 'Units', 'normalized', 'Position', [0.25, 0.1, 0.5, 0.8]);
s6 = stackedplot(overlay_tbl, stacked_overlay_vars, 'XVariable', 'Timestamp');
title('Fig 6: Stacked Plot - PŘEKRYTÍ (Surová vs Kompenzovaná)');
s6.DisplayLabels = strrep(vars_to_stack, '_', ' ');

%% ========================================================================
%% FIGURE 7: KORELAČNÍ HEATMAPA (Kompenzovaná data)
%% ========================================================================
figure(7); set(gcf, 'Name', 'Fig 7: Heatmapa kompenzovaných dat', 'Units', 'normalized', 'Position', [0.3, 0.2, 0.4, 0.6]);
vartypes = varfun(@class, final_data, 'OutputFormat', 'cell');
numeric_cols = strcmp(vartypes, 'double') | strcmp(vartypes, 'single');
numeric_final = final_data(:, numeric_cols);

[R, ~] = corrcoef(table2array(numeric_final), 'Rows', 'pairwise');

h = heatmap(numeric_final.Properties.VariableNames, numeric_final.Properties.VariableNames, R);
h.Title = 'Korelace po očištění (Cíl: Bílá pole u Temp, Hum a V5V)';
h.Colormap = sky;
h.ColorLimits = [-1 1];
h.CellLabelFormat = '%.2f';