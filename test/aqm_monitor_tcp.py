import threading
import time
import collections
import os
import math
from datetime import datetime
from pymodbus.client import ModbusTcpClient
import matplotlib.pyplot as plt
import matplotlib.animation as animation

# ==========================================
# KONFIGURACE
# ==========================================
ESP32_HOST = 'aqm.local'
MODBUS_PORT = 502
START_REG = 0
COUNT_REG = 25
CSV_FILENAME = "aqm_data_log_17_03.csv"
MAX_POINTS = 60

# ZMĚŇ NA True, POKUD CHCEŠ VIDĚT ŽIVÉ GRAFY
ENABLE_GUI = False  

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
    # Timeout nastaven na 2.0s pro rychlejší zotavení při zablokování
    client = ModbusTcpClient(ESP32_HOST, port=MODBUS_PORT, timeout=2.0)
    
    # Vytvoření hlavičky CSV, pokud soubor neexistuje
    if not os.path.exists(CSV_FILENAME):
        with open(CSV_FILENAME, "w") as f:
            f.write("Timestamp,Uptime,SO2_ppm,H2S_ppm,R_CO_kOhm,R_NH3_kOhm,R_NO2_kOhm,"
                    "Temp_C,Hum_pct,PM1,PM25,PM4,PM10,VOC,NOx,V3V3_V,V5_V\n")

    while True:
        start_loop = time.time()
        now = datetime.now()
        
        # Defaultní slovník s "prázdnými" daty (NaN), pokud čtení selže
        d = {k: math.nan for k in data_history.keys() if k != 'time'}
        d['time'] = now.strftime('%H:%M:%S')
        read_success = False

        try:
            if not client.connected:
                client.connect()
            
            # --- ČTENÍ VŠECH DAT ---
            res = client.read_input_registers(START_REG, count=COUNT_REG)
            
            if res.isError():
                # Pokud res.isError() vrátí True, došlo k chybě na vrstvě Modbus
                print(f"[{now.strftime('%H:%M:%S')}] Modbus Error: Vracím chybovou odpověď.")
                client.close() # Tvrdý reset spojení pro další iteraci
            else:
                regs = res.registers
                current_status = regs[0]
                
                # --- DEKÓDOVÁNÍ SENZORŮ ---
                v3v3_mv = regs[13]
                v5_mv = regs[14]
                co_mv, nh3_mv, no2_mv = regs[10], regs[11], regs[12]
                
                # Bezpečný výpočet odporů (prevence dělení nulou)
                def calc_r(mv, load): 
                    return (load * (mv / (v3v3_mv - mv)) / 1000.0) if (v3v3_mv - mv) > 0 else 0
                
                d['so2']   = regs[8] / 100.0
                d['h2s']   = regs[9] / 100.0
                d['co_r']  = calc_r(co_mv, 390000)
                d['nh3_r'] = calc_r(nh3_mv, 120000)
                d['no2_r'] = calc_r(no2_mv, 3900)
                d['temp']  = regs[15] / 200.0
                d['hum']   = regs[16] / 100.0
                d['pm1']   = regs[17] / 10.0
                d['pm25']  = regs[18] / 10.0
                d['pm4']   = regs[19] / 10.0
                d['pm10']  = regs[20] / 10.0
                d['voc']   = regs[21] / 10.0
                d['nox']   = regs[22] / 10.0
                d['v3v3']  = v3v3_mv / 1000.0
                d['v5']    = v5_mv / 1000.0
                
                state["wifi_ok"] = (current_status & (1 << 2)) != 0
                state["uptime"] = (regs[23] << 16) | regs[24]
                read_success = True

        except Exception as e:
            # Zachycení timeoutů, socket errorů a ModbusException (rozbité ID atd.)
            print(f"[{now.strftime('%H:%M:%S')}] Exception během čtení: {e}")
            state["wifi_ok"] = False
            client.close() # Zavřít klienta, aby se zbavil zablokovaného socketu / špatných ID

        # Uložení do paměti pro grafy (pouze pokud je GUI zapnuté, ušetří to RAM)
        if ENABLE_GUI:
            for k, v in d.items(): 
                data_history[k].append(v)

        # Zápis do CSV
        try:
            with open(CSV_FILENAME, "a") as f:
                if read_success:
                    vals = [f"{v:.2f}" for k, v in d.items() if k != 'time']
                    f.write(f"{now.strftime('%Y-%m-%d %H:%M:%S')},{state['uptime']}," + ",".join(vals) + "\n")
                else:
                    # Při chybě zapíšeme prázdné hodnoty
                    vals = ["NaN"] * (len(d) - 1)
                    f.write(f"{now.strftime('%Y-%m-%d %H:%M:%S')},{state['uptime']}," + ",".join(vals) + "\n")
        except Exception as file_e:
            print(f"Nelze zapsat do CSV: {file_e}")

        # Dynamický spánek pro zachování periody přesně 1.0 s
        elapsed = time.time() - start_loop
        time.sleep(max(0, 1.0 - elapsed))

# Spuštění sběru na pozadí
threading.Thread(target=data_collector, daemon=True).start()

# ==========================================
# GRAFICKÉ ROZHRANÍ NEBO BĚH NA POZADÍ
# ==========================================
if ENABLE_GUI:
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
        
        # Automatické škálování (Matplotlib ignoruje NaN hodnoty při škálování)
        for ax in axs: 
            ax.relim()
            ax.autoscale_view()
        
        # Hlavička okna
        wifi_str = "OK" if state["wifi_ok"] else "DISCONNECTED/ERROR"
        fig.suptitle(f"AQM Live Dashboard | Uptime: {state['uptime']}s | Wi-Fi/Modbus: {wifi_str}", fontsize=12, fontweight='bold')

    # Spuštění animace (překreslování GUI, nezávislé na vzorkování)
    ani = animation.FuncAnimation(fig, update_plot, interval=500, cache_frame_data=False)
    plt.show()

else:
    # Pokud je GUI vypnuté, držíme skript v běhu jednoduchou smyčkou
    print(f"Logování do souboru '{CSV_FILENAME}' běží na pozadí.")
    print("Grafické rozhraní je vypnuto (ENABLE_GUI = False).")
    print("Pro ukončení skriptu stiskni CTRL+C.")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nUkončuji logování a zavírám skript...")