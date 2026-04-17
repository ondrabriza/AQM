import os
import yaml
import time
import csv
from datetime import datetime
from pymodbus.client import ModbusTcpClient, ModbusSerialClient
from pymodbus.exceptions import ModbusException, ConnectionException

# --- INFLUXDB IMPORT ---
import influxdb_client
from influxdb_client.client.write_api import SYNCHRONOUS

# ================= SETTINGS =================
YAML_FILE = 'config.yaml'
CSV_DIR = 'csv_data'        # Složka pro ukládání CSV souborů
MEASUREMENT_INTERVAL = 1  # in seconds
MODBUS_MODE = "RTU"         # Options: "TCP" or "RTU"

# ================= INFLUXDB SETTINGS =================
INFLUX_URL = "http://aqm-rpi.local:8086" 
INFLUX_TOKEN = "MgHdTrItUtbh8Dasbln-uATVlDVuymvMrr_hJZ_09cbPrhg3CaHNcv3vI-SJ2pB7CxqtTJouKs6g4G51v0rPHg=="
INFLUX_ORG = "AQM"
INFLUX_BUCKET = "aqm_data" 
# ============================================

def load_configuration(target_name):
    """Loads the YAML file and separates Holding and Input registers."""
    with open(YAML_FILE, 'r', encoding='utf-8') as file:
        config = yaml.safe_load(file)
    
    aqm_device = next((mb for mb in config.get('modbus', []) if mb.get('name') == target_name), None)
    if not aqm_device:
        raise ValueError(f"Modbus device with name '{target_name}' not found in the YAML file.")
    
    holding_sensors = []
    input_sensors = []
    
    for s in aqm_device.get('sensors', []):
        # Ignore dummy registers
        if 'dummy' in s.get('name', '').lower():
            continue
            
        reg_type = s.get('input_type', 'input')
        
        # Rozdělení podle typu registru
        if reg_type == 'holding':
            # Ignorujeme control word (adresa 0) pro datové logování, 
            # logovat budeme jen R0 a Thresholds (adresa >= 2)
            if s.get('address', 0) >= 2:
                holding_sensors.append(s)
        else:
            # Input registry logujeme od adresy 2 výše
            if s.get('address', 0) >= 2:
                input_sensors.append(s)
            
    # Seřazení podle adresy
    holding_sensors.sort(key=lambda x: x['address'])
    input_sensors.sort(key=lambda x: x['address'])
    
    return aqm_device, holding_sensors, input_sensors

def process_value(regs, sensor_address, base_address, data_type, scale, swap, precision):
    """Processes raw registers based on rules defined in YAML."""
    idx = sensor_address - base_address
    
    if data_type == 'uint64':
        r0, r1, r2, r3 = regs[idx], regs[idx + 1], regs[idx + 2], regs[idx + 3]
        if swap == 'word':
            raw_value = (r3 << 48) | (r2 << 32) | (r1 << 16) | r0
        else:
            raw_value = (r0 << 48) | (r1 << 32) | (r2 << 16) | r3
    elif data_type == 'uint32':
        high, low = regs[idx], regs[idx + 1]
        if swap == 'word':
            raw_value = (low << 16) | high
        else:
            raw_value = (high << 16) | low
    elif data_type == 'int16':
        raw_value = regs[idx]
        if raw_value > 32767:
            raw_value -= 65536
    else:
        raw_value = regs[idx]

    result = raw_value * scale
    if precision == 0:
        return int(result)
    else:
        return round(result, precision)

def get_csv_filename(date_obj):
    """Generates a CSV filename based on the provided date."""
    date_str = date_obj.strftime('%Y_%m_%d')
    return os.path.join(CSV_DIR, f"aqm_data_{date_str}.csv")

def main():
    target_name = 'aqm_tcp' if MODBUS_MODE.upper() == 'TCP' else 'aqm_rtu'

    print("=================================", flush=True)
    print(f"   Data Acquisition: {MODBUS_MODE}", flush=True)
    print("=================================", flush=True)
    print(f"Loading configuration for '{target_name}' from {YAML_FILE}...", flush=True)
    
    try:
        device_config, holding_sensors, input_sensors = load_configuration(target_name)
        all_sensors = holding_sensors + input_sensors # Pro CSV hlavičku
    except Exception as e:
        print(f"Configuration error: {e}", flush=True)
        return

    slave_id = device_config.get('slave', 1)

    # 1. Výpočet bloku pro Holding Registers
    hold_min_addr = min(s['address'] for s in holding_sensors) if holding_sensors else 0
    hold_max_addr = max(s['address'] for s in holding_sensors) if holding_sensors else 0
    hold_count = (hold_max_addr - hold_min_addr) + 2 if holding_sensors else 0
    # U 32bitových MICS_NH3_R0 (adresa 6) potřebujeme přečíst i adresu 7, 
    # ale protože Thresholds končí na 10 (16bit), musíme si dát pozor.
    # Bezpečný výpočet velikosti bloku pro 32-bit registry na konci:
    if holding_sensors:
        last_sensor = holding_sensors[-1]
        extra_regs = 2 if last_sensor.get('data_type') in ['uint32', 'int32', 'float32'] else 1
        hold_count = (last_sensor['address'] - hold_min_addr) + extra_regs

    # 2. Výpočet bloku pro Input Registers
    inp_min_addr = min(s['address'] for s in input_sensors) if input_sensors else 0
    inp_max_addr = max(s['address'] for s in input_sensors) if input_sensors else 0
    if input_sensors:
        last_sensor = input_sensors[-1]
        extra_regs = 2 if last_sensor.get('data_type') in ['uint32', 'int32', 'float32'] else 1
        extra_regs = 4 if last_sensor.get('data_type') in ['uint64', 'int64'] else extra_regs
        inp_count = (last_sensor['address'] - inp_min_addr) + extra_regs
    else:
        inp_count = 0

    print(f"Holding block: addr {hold_min_addr} to {hold_min_addr + hold_count - 1} ({hold_count} registers)", flush=True)
    print(f"Input block:   addr {inp_min_addr} to {inp_min_addr + inp_count - 1} ({inp_count} registers)", flush=True)
    print(f"Total sensors to log: {len(all_sensors)}", flush=True)

    # --- INICIALIZACE INFLUXDB ---
    print("Connecting to InfluxDB...", flush=True)
    try:
        influx_client = influxdb_client.InfluxDBClient(url=INFLUX_URL, token=INFLUX_TOKEN, org=INFLUX_ORG)
        write_api = influx_client.write_api(write_options=SYNCHRONOUS)
        print("InfluxDB Client Initialized.", flush=True)
    except Exception as e:
        print(f"Failed to initialize InfluxDB client: {e}", flush=True)
        return

    # --- INICIALIZACE MODBUS ---
    if MODBUS_MODE.upper() == 'TCP':
        modbus_client = ModbusTcpClient(
            host=device_config.get('host'), 
            port=device_config.get('port', 502), 
            timeout=2 
        )
    else:
        modbus_client = ModbusSerialClient(
            port=device_config.get('port', 'COM8'),
            baudrate=device_config.get('baudrate', 115200),
            stopbits=device_config.get('stopbits', 1),
            bytesize=device_config.get('bytesize', 8),
            parity=device_config.get('parity', 'N'),
            timeout=2
        )

    print("Connecting Modbus...", flush=True)
    modbus_client.connect()

    # --- INICIALIZACE CSV SLOŽKY A SOUBORU ---
    os.makedirs(CSV_DIR, exist_ok=True)
    csv_header = ['Timestamp'] + [s['name'].replace(f"{target_name}_", '') for s in all_sensors]
    
    current_date = datetime.now().date()
    current_csv_file = get_csv_filename(current_date)
    
    file = open(current_csv_file, mode='a', newline='', encoding='utf-8')
    writer = csv.writer(file, delimiter=';')
    
    if os.path.getsize(current_csv_file) == 0:
        writer.writerow(csv_header)
        print(f"Created new CSV file: {current_csv_file}", flush=True)
    else:
        print(f"Appending to existing CSV file: {current_csv_file}", flush=True)

    print("Starting data acquisition (Press Ctrl+C to stop)...", flush=True)

    try:
        while True:
            start_time = time.time()
            now = datetime.now()
            
            # --- PŮLNOČNÍ ROTACE CSV ---
            if now.date() != current_date:
                print(f"[{now.strftime('%Y-%m-%d %H:%M:%S')}] Midnight reached. Rotating CSV file.", flush=True)
                file.close() 
                
                current_date = now.date()
                current_csv_file = get_csv_filename(current_date)
                
                file = open(current_csv_file, mode='a', newline='', encoding='utf-8')
                writer = csv.writer(file, delimiter=';')
                
                if os.path.getsize(current_csv_file) == 0:
                    writer.writerow(csv_header)
                    print(f"Created new CSV file for today: {current_csv_file}", flush=True)

            timestamp = now.strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]
            csv_row = [timestamp]
            data_valid = False 
            
            try:
                if MODBUS_MODE.upper() == 'TCP' and not modbus_client.is_socket_open():
                    modbus_client.connect()

                # --- 1. ČTENÍ HOLDING REGISTRŮ ---
                try:
                    res_hold = modbus_client.read_holding_registers(address=hold_min_addr, count=hold_count, slave=slave_id)
                except TypeError:
                    res_hold = modbus_client.read_holding_registers(address=hold_min_addr, count=hold_count, device_id=slave_id)
                
                # --- 2. ČTENÍ INPUT REGISTRŮ ---
                try:
                    res_inp = modbus_client.read_input_registers(address=inp_min_addr, count=inp_count, slave=slave_id)
                except TypeError:
                    res_inp = modbus_client.read_input_registers(address=inp_min_addr, count=inp_count, device_id=slave_id)
                
                # --- ZPRACOVÁNÍ VÝSLEDKŮ ---
                if not res_hold.isError() and not res_inp.isError():
                    hold_regs = res_hold.registers
                    inp_regs = res_inp.registers
                    data_valid = True 
                    
                    # A. Zpracování Holding senzorů (R0 a Thresholds)
                    for s in holding_sensors:
                        val = process_value(hold_regs, s['address'], hold_min_addr, s.get('data_type', 'uint16'), s.get('scale', 1.0), s.get('swap', 'none'), s.get('precision', 0))
                        csv_row.append(val)

                    # B. Zpracování Input senzorů (Ostatní)
                    for s in input_sensors:
                        val = process_value(inp_regs, s['address'], inp_min_addr, s.get('data_type', 'uint16'), s.get('scale', 1.0), s.get('swap', 'none'), s.get('precision', 0))
                        csv_row.append(val)
                    
                    try:
                        print(f"[{timestamp}] OK -> {csv_header[1]}: {csv_row[1]} | {csv_header[8]}: {csv_row[8]}", flush=True)
                    except IndexError:
                        print(f"[{timestamp}] OK -> Data uložena.", flush=True)
                        
                else:
                    print(f"[{timestamp}] Modbus Read Error (PDU/Address). Writing NaN.", flush=True)
                    for _ in all_sensors:
                        csv_row.append("NaN")
                        
            except (ModbusException, ConnectionException, Exception) as e:
                print(f"[{timestamp}] Connection Drop ({e}). Writing NaN and reconnecting...", flush=True)
                modbus_client.close() 
                for _ in all_sensors:
                    csv_row.append("NaN")

            # 3. Zápis do CSV (proběhne VŽDY)
            writer.writerow(csv_row)
            file.flush() 

            # 4. Zápis do INFLUXDB (proběhne JEN když jsou platná data)
            if data_valid:
                point = influxdb_client.Point("aqm_environment")
                
                for i, s in enumerate(all_sensors):
                    field_name = s['name'].replace(f"{target_name}_", '')
                    value = csv_row[i + 1] 
                    if value != "NaN":
                        point.field(field_name, float(value))
                
                try:
                    write_api.write(bucket=INFLUX_BUCKET, org=INFLUX_ORG, record=point)
                except Exception as e:
                    print(f"[{timestamp}] Chyba při zápisu do InfluxDB: {e}", flush=True)

            # 5. Časování smyčky
            elapsed = time.time() - start_time
            time.sleep(max(0, MEASUREMENT_INTERVAL - elapsed))

    except KeyboardInterrupt:
        print("\nAcquisition stopped by user.", flush=True)
    finally:
        if file and not file.closed:
            file.close()
        modbus_client.close()
        influx_client.close() 

if __name__ == "__main__":
    main()