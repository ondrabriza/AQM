import serial
from pymodbus.client import ModbusSerialClient
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from datetime import datetime
import collections
import os

# ==========================================
# SERIAL CONNECTION CONFIGURATION (RTU)
# ==========================================
PORT = 'COM8'  # Změň na 'COM3' apod., pokud jsi na Windows
BAUDRATE = 9600
STOPBITS = 1
BYTESIZE = 8
PARITY = 'N'
SLAVE_ID = 1

START_REG = 0
COUNT_REG = 25  # Podle struktury input_reg_params_t (0 až 24)
CSV_FILENAME = "aqm_sen55_full_log.csv"

# ==========================================
# DATA STORAGE (Posledních 60 bodů)
# ==========================================
MAX_POINTS = 60
time_x = collections.deque(maxlen=MAX_POINTS)

# Plyny a Odpory
data_so2 = collections.deque(maxlen=MAX_POINTS)
data_h2s = collections.deque(maxlen=MAX_POINTS)
data_res_co  = collections.deque(maxlen=MAX_POINTS)
data_res_nh3 = collections.deque(maxlen=MAX_POINTS)
data_res_no2 = collections.deque(maxlen=MAX_POINTS)

# SEN55 PM a Indexy
data_pm1  = collections.deque(maxlen=MAX_POINTS)
data_pm25 = collections.deque(maxlen=MAX_POINTS)
data_pm4  = collections.deque(maxlen=MAX_POINTS)
data_pm10 = collections.deque(maxlen=MAX_POINTS)
data_voc  = collections.deque(maxlen=MAX_POINTS)
data_nox  = collections.deque(maxlen=MAX_POINTS)

# Klima a Napětí
data_temp = collections.deque(maxlen=MAX_POINTS)
data_hum  = collections.deque(maxlen=MAX_POINTS)
data_v3v3 = collections.deque(maxlen=MAX_POINTS)
data_v5   = collections.deque(maxlen=MAX_POINTS)

# Inicializace Modbus RTU klienta
print(f"Připojuji se přes Modbus RTU na portu {PORT}...")
client = ModbusSerialClient(
    port=PORT,
    baudrate=BAUDRATE,
    stopbits=STOPBITS,
    bytesize=BYTESIZE,
    parity=PARITY,
)

if client.connect():
    print("Úspěšně připojeno!")
else:
    print(f"Chyba připojení na port {PORT}. Zkontroluj zapojení a práva.")
    exit()

# Inicializace CSV souboru
if not os.path.exists(CSV_FILENAME):
    with open(CSV_FILENAME, "w") as f:
        f.write("Timestamp,Uptime_sec,SO2_ppm,H2S_ppm,R_CO_kOhm,R_NH3_kOhm,R_NO2_kOhm,"
                "Temp_C,Humidity_pct,PM1.0,PM2.5,PM4.0,PM10.0,VOC_Index,NOx_Index,V3V3,V5V\n")

# Vytvoření grafického okna (8 podgrafů pod sebou se sdílenou osou X)
fig, axs = plt.subplots(8, 1, figsize=(11, 20), sharex=True)
fig.canvas.manager.set_window_title('AQM Dashboard - Kompletní Data (RTU)')

# Rozbalení os pro jednodušší přístup
(ax_gas, ax_res_co_nh3, ax_res_no2, ax_pm, ax_temp, ax_hum, ax_aqi, ax_volt) = axs

def update_plot(frame):
    result = client.read_input_registers(START_REG, count=COUNT_REG, device_id=SLAVE_ID)
    
    if not result.isError():
        regs = result.registers
        
        # --- DEKÓDOVÁNÍ REGISTRŮ PODLE STRUKTURY ---
        status_word = regs[0]
        
        # Plyny
        so2_ppm = regs[8] / 100.0
        h2s_ppm = regs[9] / 100.0
        co_mv   = regs[10]
        nh3_mv  = regs[11]
        no2_mv  = regs[12]
        
        # Napětí
        v3v3_mv    = regs[13]
        v3v3_volts = v3v3_mv / 1000.0
        v5_volts   = regs[14] / 1000.0
        
        temp_raw = regs[15]
        temp_c = temp_raw / 200.0   # Scale factor 200
        hum_pct = regs[16] / 100.0  # Scale factor 100
        
        # Částice a indexy (SEN55)
        pm1_0 = regs[17] / 10.0     # Scale factor 10
        pm2_5 = regs[18] / 10.0     # Scale factor 10
        pm4_0 = regs[19] / 10.0     # Scale factor 10
        pm10_0 = regs[20] / 10.0    # Scale factor 10
        voc_index = regs[21] / 10.0 # Scale factor 10
        nox_index = regs[22] / 10.0 # Scale factor 10
        
        # Systém
        uptime_sec = (regs[23] << 16) | regs[24]
        wifi_ok = (status_word & (1 << 0)) != 0
        relay_on = (status_word & (1 << 1)) != 0
        
        # --- VÝPOČET ODPORU SENZORŮ (MiCS) ---
        R_CO_LOAD  = 390000
        R_NH3_LOAD = 120000
        R_NO2_LOAD = 3900
        
        # Ochrana proti dělení nulou
        co_res  = R_CO_LOAD * (co_mv / (v3v3_mv - co_mv)) if (v3v3_mv - co_mv) > 0 else 0
        nh3_res = R_NH3_LOAD * (nh3_mv / (v3v3_mv - nh3_mv)) if (v3v3_mv - nh3_mv) > 0 else 0
        no2_res = R_NO2_LOAD * (no2_mv / (v3v3_mv - no2_mv)) if (v3v3_mv - no2_mv) > 0 else 0

        # --- UKLÁDÁNÍ DAT ---
        now = datetime.now()
        now_str = now.strftime('%H:%M:%S')
        full_timestamp = now.strftime('%Y-%m-%d %H:%M:%S')
        
        time_x.append(now_str)
        data_so2.append(so2_ppm)
        data_h2s.append(h2s_ppm)
        data_res_co.append(co_res / 1000.0)
        data_res_nh3.append(nh3_res / 1000.0)
        data_res_no2.append(no2_res / 1000.0)
        data_temp.append(temp_c)
        data_hum.append(hum_pct)
        data_pm1.append(pm1_0)
        data_pm25.append(pm2_5)
        data_pm4.append(pm4_0)
        data_pm10.append(pm10_0)
        data_voc.append(voc_index)
        data_nox.append(nox_index)
        data_v3v3.append(v3v3_volts)
        data_v5.append(v5_volts)
        
        # --- CSV LOGOVÁNÍ ---
        with open(CSV_FILENAME, "a") as f:
            f.write(f"{full_timestamp},{uptime_sec},{so2_ppm:.2f},{h2s_ppm:.2f},"
                    f"{co_res/1000.0:.2f},{nh3_res/1000.0:.2f},{no2_res/1000.0:.2f},"
                    f"{temp_c:.1f},{hum_pct:.1f},{pm1_0},{pm2_5},{pm4_0},{pm10_0},"
                    f"{voc_index},{nox_index},{v3v3_volts:.3f},{v5_volts:.3f}\n")
        
        # --- KRESLENÍ GRAFŮ ---
        for ax in axs:
            ax.clear()
            ax.grid(True)
            # Zákaz vědeckého zápisu (+2.251e1) na všech osách Y:
            ax.ticklabel_format(useOffset=False, style='plain', axis='y')
        
        # Plot 1: Gases
        ax_gas.plot(time_x, data_so2, label="SO2", color='blue', linestyle='dotted')
        ax_gas.plot(time_x, data_h2s, label="H2S", color='green', linestyle='dotted')
        ax_gas.set_title(f"Gas Concentrations | Wi-Fi: {'OK' if wifi_ok else 'DISCONNECTED'} | Relay: {'ON' if relay_on else 'OFF'}")
        ax_gas.set_ylabel("PPM")
        ax_gas.legend(loc="upper left")
        
        # Plot 2: Sensor Resistances (CO & NH3)
        ax_res_co_nh3.plot(time_x, data_res_co,  label="R_CO",  color='purple', linestyle='dotted')
        ax_res_co_nh3.plot(time_x, data_res_nh3, label="R_NH3", color='cyan', linestyle='dotted')
        ax_res_co_nh3.set_title("MiCS Sensor Resistances (CO & NH3)")
        ax_res_co_nh3.set_ylabel("kΩ")
        ax_res_co_nh3.legend(loc="upper left", ncol=2)

        # Plot 3: Sensor Resistance (NO2)
        ax_res_no2.plot(time_x, data_res_no2, label="R_NO2", color='brown', linestyle='dotted')
        ax_res_no2.set_title("MiCS Sensor Resistance (NO2)")
        ax_res_no2.set_ylabel("kΩ")
        ax_res_no2.legend(loc="upper left")

        # Plot 4: PM Particles
        ax_pm.plot(time_x, data_pm1,  label="PM1.0", color='grey', linestyle='dotted')
        ax_pm.plot(time_x, data_pm25, label="PM2.5", color='olive', linestyle='dotted')
        ax_pm.plot(time_x, data_pm4,  label="PM4.0", color='darkorange', linestyle='dotted')
        ax_pm.plot(time_x, data_pm10, label="PM10.0", color='saddlebrown', linestyle='dotted')
        ax_pm.set_title("Particulate Matter (μg/m³)")
        ax_pm.set_ylabel("μg/m³")
        ax_pm.legend(loc="upper left", ncol=4)

        # Plot 5: Temperature
        ax_temp.plot(time_x, data_temp, label="Temperature", color='red', linestyle='dotted')
        ax_temp.set_title("Temperature")
        ax_temp.set_ylabel("°C")
        ax_temp.legend(loc="upper left")

        # Plot 6: Humidity
        ax_hum.plot(time_x, data_hum, label="Humidity", color='blue', linestyle='dotted')
        ax_hum.set_title("Relative Humidity")
        ax_hum.set_ylabel("% RH")
        ax_hum.set_ylim([0, 100])
        ax_hum.legend(loc="upper left")

        # Plot 7: VOC & NOx Indices
        ax_aqi.plot(time_x, data_voc, label="VOC Index", color='magenta', linestyle='dotted')
        ax_aqi.plot(time_x, data_nox, label="NOx Index", color='darkblue', linestyle='dotted')
        ax_aqi.set_title("Air Quality Indices (SEN55)")
        ax_aqi.set_ylabel("Index")
        ax_aqi.legend(loc="upper left", ncol=2)

        # Plot 8: Voltages
        ax_volt.plot(time_x, data_v3v3, label="3.3V Rail", color='teal', linestyle='dotted')
        ax_volt.plot(time_x, data_v5,   label="5.0V Rail", color='red', linestyle='dotted')
        ax_volt.set_title("Hardware Voltages")
        ax_volt.set_ylabel("Voltage (V)")
        ax_volt.set_ylim([0.0, 5.5]) 
        ax_volt.legend(loc="upper left", ncol=2)
        ax_volt.tick_params(axis='x', rotation=45) 
        
        plt.tight_layout()
        plt.subplots_adjust(left=0.08, right=0.95, top=0.97, bottom=0.05, hspace=0.4)  
        
    else:
        print(f"[{datetime.now().strftime('%H:%M:%S')}] Modbus RTU Error!")

# Spuštění animace každých 1000 ms
ani = animation.FuncAnimation(fig, update_plot, interval=500, cache_frame_data=False)

plt.show()
client.close()