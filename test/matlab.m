clc
close all
clear 


%% KOMPLEXNÍ ANALÝZA: Gating & Kompenzace (Vlhkost + Teplota)
filename = 'data_17_03.csv'; 

% 1. Import dat
opts = detectImportOptions(filename);
opts.VariableNamingRule = 'preserve'; 
data = readtable(filename, opts);

% 2. Oříznutí na poslední sekvenci (Restart detekce od 177+)
diffs = diff(data.Uptime);
restarts = find(diffs < 0);
if ~isempty(restarts)
    last_seq = restarts(end) + 1;
    idx_177 = find(data.Uptime(last_seq:end) >= 0, 1, 'first');
    start_row = last_seq + idx_177 - 1;
else
    start_row = find(data.Uptime >= 0, 1, 'first');
    if isempty(start_row), start_row = 1; end
end
clean_data = data(start_row:end, :);

figure
s = stackedplot(clean_data, 'XVariable', 'Uptime');
grid on

% --- KROK 1: GATING (Odstranění glitchů v napájení) ---
V5V_threshold = 4.9; 
V3V3_threshold = 3.3;
power_mask = (clean_data.V5V >= V5V_threshold) & (clean_data.V3V3 >= V3V3_threshold);

% Vytvoříme kopii tabulky pro očištěná data
final_data = clean_data;

% --- KROK 2: VÍCENÁSOBNÁ KOMPENZACE (VLHKOST + TEPLOTA) ---
% Definujeme sloupce, které chceme kompenzovat
vars_to_fix = {'SO2_ppm', 'H2S_ppm', 'R_CO_kOhm', 'R_NH3_kOhm', 'R_NO2_kOhm', 'VOC', 'NOx'};

fprintf('--- Start vícenásobné kompenzace (T + RH) ---\n');
x_hum = clean_data.Humidity_pct;
x_temp = clean_data.Temp_C;

for i = 1:length(vars_to_fix)
    var_name = vars_to_fix{i};
    y_raw = clean_data.(var_name);
    
    % Najdeme indexy, kde jsou data validní (napájení je OK a nejsou to NaN)
    valid_idx = power_mask & ~isnan(y_raw) & ~isnan(x_hum) & ~isnan(x_temp);
    
    % Kontrola, zda se hodnota vůbec mění (ochrana proti konstantnímu šumu)
    if sum(valid_idx) > 10 && std(y_raw(valid_idx)) > 0
        % Sestavení matice X pro výpočet: [Absolutní člen (1), Vlhkost, Teplota]
        X_matrix = [ones(sum(valid_idx), 1), x_hum(valid_idx), x_temp(valid_idx)];
        Y_vector = y_raw(valid_idx);
        
        % Výpočet koeficientů metodou nejmenších čtverců
        % beta(1) = offset, beta(2) = koeficient vlhkosti, beta(3) = koeficient teploty
        beta = X_matrix \ Y_vector; 
        
        % Aplikace odhadnutého modelu na celou datovou řadu
        y_model = beta(1) + beta(2) * x_hum + beta(3) * x_temp;
        
        % Očištění surových dat (odečtení vlivu) a zachování průměrné hodnoty
        y_final = (y_raw - y_model) + mean(y_raw(valid_idx));
        
        % Uložení do finální tabulky
        final_data.(var_name) = y_final;
        fprintf('Senzor %s: Kompenzováno (RH: %.4f, Temp: %.4f)\n', var_name, beta(2), beta(3));
    else
        fprintf('Senzor %s: Přeskočeno (konstantní nebo chybí data)\n', var_name);
    end
end

% --- KROK 3: FINÁLNÍ GATING NA VŠECHNA DATA ---
% Aplikujeme masku - během výpadku napájení změníme data na NaN
var_names = final_data.Properties.VariableNames;
for i = 1:length(var_names)
    % Nechceme vymazat časovou osu, jen měřené hodnoty
    if ~strcmp(var_names{i}, 'Uptime') && ~strcmp(var_names{i}, 'Timestamp')
        final_data.(var_names{i})(~power_mask) = NaN;
    end
end

% --- VIZUALIZACE ---

% 1. Stacked Plot vyčištěných dat
figure('Name', 'Vyčištěná data (Gating + Kompenzace T&RH)', 'Units', 'normalized', 'Position', [0.05, 0.1, 0.45, 0.8]);
s = stackedplot(final_data, 'XVariable', 'Uptime');
s.DisplayLabels = strrep(final_data.Properties.VariableNames(2:end), '_', ' ');
grid on;
title('Data po odstranění glitchů a vlivu Vlhkosti & Teploty');

% 2. Korelační Heatmapa (Opravená)
figure('Name', 'Heatmapa po očištění', 'Units', 'normalized', 'Position', [0.5, 0.1, 0.45, 0.8]);

% Vybereme numerické sloupce (od druhého sloupce, pokud je Timestamp první)
numeric_final = final_data(:, 3:end);
[R, P] = corrcoef(table2array(numeric_final), 'Rows', 'pairwise');

h = heatmap(numeric_final.Properties.VariableNames, numeric_final.Properties.VariableNames, R);
h.Title = 'Korelace po očištění (Cíl: Bílá pole u Temp, Hum a V5V)';
h.Colormap = sky;
h.ColorLimits = [-1 1];
h.XLabel = 'Měřená veličina';
h.YLabel = 'Měřená veličina';
h.CellLabelFormat = '%.3f'; % Zobrazí hodnoty na 3 desetinná místa

fprintf('--- Hotovo. Zahozeno %.2f %% dat kvůli glitchům. ---\n', (sum(~power_mask)/length(power_mask))*100);