% =========================================================================
% ADVANCED MiCS-6814 MULTI-VARIABLE CALIBRATION (Temp + Humidity + NOx)
% Applied to FULL DATASET (Clean + Polluted)
% =========================================================================

clc; clear; close all;

filename = 'data_14_04.csv';
fprintf('Loading data: %s...\n', filename);

% 1. ROBUST PARSING
rawText = fileread(filename);
lines = splitlines(string(rawText));
lines(lines == "") = []; 
headerIdx = find(contains(lines, '_time') & contains(lines, '_field'), 1);
headers = split(lines(headerIdx), ',');
timeIdx = find(headers == "_time");
fieldIdx = find(headers == "_field");
valIdx = find(headers == "_value");
dataLines = lines(headerIdx+1:end);
dataLines(startsWith(dataLines, '#')) = []; 
dataMat = split(dataLines, ',');
rawValues = str2double(dataMat(:, valIdx));
noZ = strrep(dataMat(:, timeIdx), 'Z', '');
cleanTimes = extractBefore(noZ, '.');
idx = ismissing(cleanTimes);
cleanTimes(idx) = noZ(idx); 
timeGroup = datetime(cleanTimes, 'InputFormat', 'yyyy-MM-dd''T''HH:mm:ss');
cleanData = table(timeGroup, dataMat(:, fieldIdx), rawValues, 'VariableNames', {'Time', 'Field', 'Value'});

% PIVOT DATA (Align sensors)
pivotData = unstack(cleanData, 'Value', 'Field', 'AggregationFunction', @mean);

% 2. EXTRACT FULL DATASET (Before any filtering)
all_T = pivotData.temperature;
all_H = pivotData.humidity;
all_R = pivotData.mics_red;
all_VOC = pivotData.voc_index;
all_Time = pivotData.Time;

% 3. MULTI-STAGE FILTERING (Calibration Model Fitting)
fprintf('Filtering clean air for model calibration...\n');
cols = pivotData.Properties.VariableNames;
validIdx = true(height(pivotData), 1);
if ismember('voc_index', cols), validIdx = validIdx & (pivotData.voc_index < 80); end
if ismember('nox_index', cols), validIdx = validIdx & (pivotData.nox_index <= 2); end

baselineData = pivotData(validIdx, :);
cleanRows = ~isnan(baselineData.temperature) & ~isnan(baselineData.humidity) & ~isnan(baselineData.mics_red);

T_cal = baselineData.temperature(cleanRows);
H_cal = baselineData.humidity(cleanRows);
R_cal = baselineData.mics_red(cleanRows);
time_cal = baselineData.Time(cleanRows);

if isempty(T_cal), error('Not enough valid clean data points for calibration.'); end

% 4. CALCULATE MODELS (Individual and Combined from Clean Data)
fprintf('Calculating regression models...\n');

% Combined Temp + Humidity (2D Regression)
X_cal = [T_cal, H_cal, ones(size(T_cal))];
coeffs = X_cal \ R_cal; 
c_temp = coeffs(1);
c_hum = coeffs(2);
intercept = coeffs(3);

% 5. APPLY COMPENSATION TO THE FULL DATASET
fprintf('Applying model to the entire dataset...\n');
% expected_clean = model based on current T and H
expected_all = (c_temp * all_T) + (c_hum * all_H) + intercept;
R_comp_All = all_R ./ expected_all;

% 6. FIGURE 1: ORIGINAL COMPARISON (Clean Data Only)
figure('Name', 'Calibration Model Analysis', 'Position', [50, 50, 1200, 800]);
subplot(2, 1, 1);
plot(time_cal, R_cal, 'Color', [0.7 0.7 0.7], 'DisplayName', 'Raw Ratio (Clean Air)'); hold on;
plot(time_cal, (c_temp*T_cal + c_hum*H_cal + intercept), 'r', 'LineWidth', 1.5, 'DisplayName', 'Model Fit');
grid on; ylabel('Ratio Value'); title('Baseline Model Alignment (Clean Air Only)'); legend;

subplot(2, 3, 4);
scatter(T_cal, R_cal, 10, 'filled', 'MarkerFaceAlpha', 0.2); hold on;
plot(linspace(min(T_cal), max(T_cal)), polyfit(T_cal, R_cal, 1)*(1:2)', 'r', 'LineWidth', 2); % Simple trendline
title('T-Drift (Clean Air)'); xlabel('Temp [°C]'); ylabel('Ratio'); grid on;

subplot(2, 3, 5);
scatter(H_cal, R_cal, 10, 'filled', 'MarkerFaceAlpha', 0.2);
title('H-Drift (Clean Air)'); xlabel('Humidity [%]'); grid on;

subplot(2, 3, 6);
plot(time_cal, R_cal ./ (c_temp*T_cal + c_hum*H_cal + intercept), 'r');
title('Compensated Baseline'); xlabel('Time'); grid on; yline(1.0, '--k');

% 7. FIGURE 2: FULL DATASET PERFORMANCE (Clean + Polluted)
figure('Name', 'Full Dataset: Raw vs. Compensated', 'Position', [100, 100, 1200, 850]);

% Subplot A: Raw Signal & Environmental Sensors
subplot(3, 1, 1);
yyaxis left
plot(all_Time, all_R, 'k', 'LineWidth', 1, 'DisplayName', 'RAW Ratio (Uncompensated)');
ylabel('Raw Ratio [Rs/R0]'); hold on;
yyaxis right
plot(all_Time, all_T, 'r', 'DisplayName', 'Temp');
plot(all_Time, all_H, 'b', 'DisplayName', 'Hum');
ylabel('Environment'); title('Raw Sensor Data & Environmental Conditions');
grid on; legend('location', 'best');

% Subplot B: Compensated Result (THE GOAL)
subplot(3, 1, 2);
plot(all_Time, R_comp_All, 'g', 'LineWidth', 1.2, 'DisplayName', 'COMPENSATED Ratio (Full T+H)');
hold on;
yline(1.0, '--k', 'Clean Air Baseline', 'LineWidth', 1.5);
ylabel('Compensated Ratio'); 
title('Result: Normalized Baseline (Spikes = Real Pollution)'); 
ylim([0.2 1.3]); grid on; legend('location', 'best');

% Subplot C: VOC Reference for Verification
subplot(3, 1, 3);
area(all_Time, all_VOC, 'FaceColor', [0.8 0.8 1], 'EdgeColor', 'b', 'DisplayName', 'SEN55 VOC Index');
ylabel('VOC Index'); title('Verification: SEN55 VOC Reference Signal');
grid on; legend('location', 'best');

% 8. C++ OUTPUT GENERATION
fprintf('\n======================================================\n');
fprintf('                CALIBRATION RESULTS\n');
fprintf('======================================================\n');
fprintf('Combined Model: Ratio = (%.4f * T) + (%.4f * H) + %.4f\n', c_temp, c_hum, intercept);
fprintf('\nUpdated C++ code for your firmware:\n\n');

fprintf('float raw_ratio = read_mics_red_ratio(); \n');
fprintf('float T = read_sen55_temp();\n');
fprintf('float H = read_sen55_hum();\n\n');

fprintf('// 1. Calculate the theoretical clean air ratio for current conditions\n');
fprintf('float expected_clean = (%.4f * T) + (%.4f * H) + %.4f;\n', c_temp, c_hum, intercept);

fprintf('\n// 2. Compensate by dividing (normalizes baseline to 1.0)\n');
fprintf('float comp_ratio = raw_ratio / expected_clean;\n\n');

fprintf('// 3. Prevent math errors and calculate PPM\n');
fprintf('if (comp_ratio > 1.0) comp_ratio = 1.0; \n');
fprintf('float ppm_CO = A_CO * pow(comp_ratio, B_CO);\n');
fprintf('======================================================\n');