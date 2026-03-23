import threading
import time
import collections
import os
from datetime import datetime
from pymodbus.client import ModbusTcpClient
import matplotlib.pyplot as plt
import matplotlib.animation as animation

# ==========================================
# KONFIGURACE
# ==========================================
ESP32_HOST = '192.168.0.147'
MODBUS_PORT = 502
START_REG = 0
COUNT_REG = 25
CSV_FILENAME = "aqm_data_log_17_03.csv"
MAX_POINTS = 60

# Datové kontejnery pro grafy (Thread-safe)
data_history = {k: collections.deque(maxlen=MAX_POINTS) for k in 
                ['time', 'so2', 'h2s', 'co_r', 'nh3_r', 'no2_r', 'temp', 'hum', 
                 'pm1', 'pm25', 'pm4', 'pm10', 'voc', 'nox', 'v3v3', 'v5']}

# Globální stav sdílený mezi vlákny
state = {
    "wifi_ok": False, 
    "uptime": 0
}

# ==========================================
# SBĚRNÉ VLÁKNO (Vzorkování přesně 1s)
# ==========================================
def data_collector():
    client = ModbusTcpClient(ESP32_HOST, port=MODBUS_PORT)
    
    # Vytvoření hlavičky CSV, pokud soubor neexistuje
    if not os.path.exists(CSV_FILENAME):
        with open(CSV_FILENAME, "w") as f:
            f.write("Timestamp,Uptime,SO2_ppm,H2S_ppm,R_CO_kOhm,R_NH3_kOhm,R_NO2_kOhm,"
                    "Temp_C,Hum_pct,PM1,PM25,PM4,PM10,VOC,NOx,V3V3_V,V5_V\n")

    while True:
        start_loop = time.time()
        try:
            if not client.connected:
                client.connect()
            
            # --- ČTENÍ VŠECH DAT ---
            res = client.read_input_registers(START_REG, count=COUNT_REG)
            
            if not res.isError():
                regs = res.registers
                current_status = regs[0]
                now = datetime.now()
                
                # --- DEKÓDOVÁNÍ SENZORŮ ---
                v3v3_mv = regs[13]
                v5_mv = regs[14]
                co_mv, nh3_mv, no2_mv = regs[10], regs[11], regs[12]
                
                # Bezpečný výpočet odporů (prevence dělení nulou)
                def calc_r(mv, load): 
                    return (load * (mv / (v3v3_mv - mv)) / 1000.0) if (v3v3_mv - mv) > 0 else 0
                
                d = {
                    'time':  now.strftime('%H:%M:%S'),
                    'so2':   regs[8] / 100.0,
                    'h2s':   regs[9] / 100.0,
                    'co_r':  calc_r(co_mv, 390000),
                    'nh3_r': calc_r(nh3_mv, 120000),
                    'no2_r': calc_r(no2_mv, 3900),
                    'temp':  regs[15] / 200.0,
                    'hum':   regs[16] / 100.0,
                    'pm1':   regs[17] / 10.0,
                    'pm25':  regs[18] / 10.0,
                    'pm4':   regs[19] / 10.0,
                    'pm10':  regs[20] / 10.0,
                    'voc':   regs[21] / 10.0,
                    'nox':   regs[22] / 10.0,
                    'v3v3':  v3v3_mv / 1000.0,
                    'v5':    v5_mv / 1000.0
                }

                # Uložení do paměti pro grafy
                for k, v in d.items(): 
                    data_history[k].append(v)
                
                state["wifi_ok"] = (current_status & (1 << 0)) != 0
                state["uptime"] = (regs[23] << 16) | regs[24]

                # Zápis do CSV
                with open(CSV_FILENAME, "a") as f:
                    vals = [f"{v:.2f}" for k, v in d.items() if k != 'time']
                    f.write(f"{now.strftime('%Y-%m-%d %H:%M:%S')},{state['uptime']}," + ",".join(vals) + "\n")
                    
        except Exception as e:
            # Při chybě spojení jen zaznamenáme stav, smyčka zkusí reconnect v dalším cyklu
            state["wifi_ok"] = False

        # Dynamický spánek pro zachování periody přesně 1.0 s
        elapsed = time.time() - start_loop
        time.sleep(max(0, 1.0 - elapsed))

# Spuštění sběru na pozadí
threading.Thread(target=data_collector, daemon=True).start()

# ==========================================
# GRAFICKÉ ROZHRANÍ (Matplotlib)
# ==========================================
fig, axs = plt.subplots(6, 1, figsize=(12, 18), sharex=True)
plt.subplots_adjust(bottom=0.08, hspace=0.3, top=0.95)
fig.canvas.manager.set_window_title('AQM Dashboard')

# Společný styl: body 'o' spojené tečkovanou čárou ':'
p = {'marker': 'o', 'linestyle': ':', 'markersize': 3, 'linewidth': 1}

# 1. Koncentrace plynů (PPM)
l_so2, = axs[0].plot([], [], label="SO2", color="blue", **p)
l_h2s, = axs[0].plot([], [], label="H2S", color="green", **p)
axs[0].set_ylabel("PPM")

# 2. Odpory MiCS (kOhm)
l_co_r,  = axs[1].plot([], [], label="R_CO", color="purple", **p)
l_nh3_r, = axs[1].plot([], [], label="R_NH3", color="brown", **p)
l_no2_r, = axs[1].plot([], [], label="R_NO2", color="orange", **p)
axs[1].set_ylabel("kΩ")

# 3. Teplota a Vlhkost
l_temp, = axs[2].plot([], [], label="Temp", color="red", **p)
l_hum,  = axs[2].plot([], [], label="Hum", color="cyan", **p)
axs[2].set_ylabel("°C / %")

# 4. Prachové částice (PM)
l_pm1,  = axs[3].plot([], [], label="PM1.0", color="gray", **p)
l_pm25, = axs[3].plot([], [], label="PM2.5", color="orange", **p)
l_pm4,  = axs[3].plot([], [], label="PM4.0", color="brown", **p)
l_pm10, = axs[3].plot([], [], label="PM10", color="black", **p)
axs[3].set_ylabel("μg/m³")

# 5. Indexy kvality ovzduší (AQI)
l_voc, = axs[4].plot([], [], label="VOC Index", color="magenta", **p)
l_nox, = axs[4].plot([], [], label="NOx Index", color="darkblue", **p)
axs[4].set_ylabel("Index")

# 6. Napájecí napětí (Voltage)
l_v3v3, = axs[5].plot([], [], label="3.3V", color="teal", **p)
l_v5,   = axs[5].plot([], [], label="5.0V", color="darkred", **p)
axs[5].set_ylabel("Voltage [V]")

# Společné nastavení grafů
for ax in axs:
    ax.grid(True, alpha=0.4)
    ax.legend(loc="upper left", fontsize='small', ncol=4)
    ax.ticklabel_format(useOffset=False, style='plain', axis='y')

# --- AKTUALIZACE GRAFIKY ---
def update_plot(frame):
    if not data_history['time']: 
        return
        
    x = list(range(len(data_history['time'])))
    
    # Aktualizace datových řad
    l_so2.set_data(x, list(data_history['so2']))
    l_h2s.set_data(x, list(data_history['h2s']))
    l_co_r.set_data(x, list(data_history['co_r']))
    l_nh3_r.set_data(x, list(data_history['nh3_r']))
    l_no2_r.set_data(x, list(data_history['no2_r']))
    l_temp.set_data(x, list(data_history['temp']))
    l_hum.set_data(x, list(data_history['hum']))
    l_pm1.set_data(x, list(data_history['pm1']))
    l_pm25.set_data(x, list(data_history['pm25']))
    l_pm4.set_data(x, list(data_history['pm4']))
    l_pm10.set_data(x, list(data_history['pm10']))
    l_voc.set_data(x, list(data_history['voc']))
    l_nox.set_data(x, list(data_history['nox']))
    l_v3v3.set_data(x, list(data_history['v3v3']))
    l_v5.set_data(x, list(data_history['v5']))

    # Nastavení osy X (časy) pouze na nejspodnějším grafu
    axs[5].set_xticks(x)
    axs[5].set_xticklabels(list(data_history['time']), rotation=45, fontsize=8)
    
    # Automatické škálování
    for ax in axs: 
        ax.relim()
        ax.autoscale_view()
    
    # Hlavička okna
    wifi_str = "OK" if state["wifi_ok"] else "DISCONNECTED"
    fig.suptitle(f"AQM Live Dashboard | Uptime: {state['uptime']}s | Wi-Fi: {wifi_str}", fontsize=12, fontweight='bold')

# Spuštění animace (překreslování GUI, nezávislé na vzorkování)
ani = animation.FuncAnimation(fig, update_plot, interval=500, cache_frame_data=False)
plt.show()