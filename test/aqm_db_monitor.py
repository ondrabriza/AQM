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
CSV_FILE = 'aqm_data_db.csv'
MEASUREMENT_INTERVAL = 1  # in seconds
MODBUS_MODE = "RTU"         # Options: "TCP" or "RTU"

# ================= INFLUXDB SETTINGS =================
INFLUX_URL = "http://aqm-rpi.local:8086" # Nebo 127.0.0.1 pokud skript běží přímo na RPi
INFLUX_TOKEN = "MgHdTrItUtbh8Dasbln-uATVlDVuymvMrr_hJZ_09cbPrhg3CaHNcv3vI-SJ2pB7CxqtTJouKs6g4G51v0rPHg=="
INFLUX_ORG = "AQM"
INFLUX_BUCKET = "aqm_data" # Ujisti se, že se bucket v databázi jmenuje přesně takto
# ============================================

def load_configuration(target_name):
    """Loads the YAML file and filters Input registers for the target device."""
    with open(YAML_FILE, 'r', encoding='utf-8') as file:
        config = yaml.safe_load(file)
    
    # Find configuration for the selected target (aqm_tcp or aqm_rtu)
    aqm_device = next((mb for mb in config.get('modbus', []) if mb.get('name') == target_name), None)
    if not aqm_device:
        raise ValueError(f"Modbus device with name '{target_name}' not found in the YAML file.")
    
    # Filter sensors (ignore dummy registers and holding registers)
    sensors = []
    for s in aqm_device.get('sensors', []):
        # Keep only input data (address >= 2) and ignore the dummy sensor
        if s.get('address', 0) >= 2 and 'dummy' not in s.get('name', '').lower():
            sensors.append(s)
            
    # Sort sensors by address to ensure correct processing order
    sensors.sort(key=lambda x: x['address'])
    
    return aqm_device, sensors

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

def main():
    target_name = 'aqm_tcp' if MODBUS_MODE.upper() == 'TCP' else 'aqm_rtu'

    print("=================================", flush=True)
    print(f"   Data Acquisition: {MODBUS_MODE}", flush=True)
    print("=================================", flush=True)
    print(f"Loading configuration for '{target_name}' from {YAML_FILE}...", flush=True)
    
    try:
        device_config, sensors = load_configuration(target_name)
    except Exception as e:
        print(f"Configuration error: {e}", flush=True)
        return

    min_address = min(s['address'] for s in sensors)
    max_address = max(s['address'] for s in sensors)
    register_count = (max_address - min_address) + 2 
    slave_id = device_config.get('slave', 1)

    print(f"Target block: address {min_address} to {min_address + register_count - 1} ({register_count} registers total)", flush=True)
    print(f"Found {len(sensors)} sensors to log.", flush=True)

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

    csv_header = ['Timestamp'] + [s['name'].replace(f"{target_name}_", '') for s in sensors]
    file_exists = os.path.isfile(CSV_FILE)

    with open(CSV_FILE, mode='a', newline='', encoding='utf-8') as file:
        writer = csv.writer(file, delimiter=';')
        
        if not file_exists:
            writer.writerow(csv_header)
            print(f"Created new CSV file: {CSV_FILE}", flush=True)
        else:
            print(f"Appending to existing CSV file: {CSV_FILE}", flush=True)

        print("Starting data acquisition (Press Ctrl+C to stop)...", flush=True)

        try:
            while True:
                start_time = time.time()
                timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]
                csv_row = [timestamp]
                data_valid = False # Pomocná vlajka pro InfluxDB
                
                try:
                    # 1. Proaktivní kontrola TCP spojení
                    if MODBUS_MODE.upper() == 'TCP' and not modbus_client.is_socket_open():
                        modbus_client.connect()

                    # 2. Samotné čtení dat
                    try:
                        # 1. Pokus pro nový pymodbus 3.x (Linux/RPi)
                        result = modbus_client.read_input_registers(address=min_address, count=register_count, slave=slave_id)
                    except TypeError:
                        # 2. Záložní pokus pro starý pymodbus 2.x (Windows)
                        # Ve starších verzích se pro adresu používal parametr 'unit' (případně bez pojmenování)
                        result = modbus_client.read_input_registers(address=min_address, count=register_count, device_id=slave_id)
                    
                    if not result.isError():
                        regs = result.registers
                        data_valid = True # Data jsou v pořádku, povolíme zápis do InfluxDB
                        
                        for s in sensors:
                            value = process_value(
                                regs=regs,
                                sensor_address=s['address'],
                                base_address=min_address,
                                data_type=s.get('data_type', 'uint16'),
                                scale=s.get('scale', 1.0),
                                swap=s.get('swap', 'none'),
                                precision=s.get('precision', 0)
                            )
                            csv_row.append(value)
                        
                        try:
                            print(f"[{timestamp}] OK -> {csv_header[1]}: {csv_row[1]} | {csv_header[9]}: {csv_row[9]}", flush=True)
                        except IndexError:
                            print(f"[{timestamp}] OK -> Data uložena.", flush=True)
                            
                    else:
                        print(f"[{timestamp}] Chyba PDU nebo adresy. Zapisuji NaN.", flush=True)
                        for _ in sensors:
                            csv_row.append("NaN")
                            
                except (ModbusException, ConnectionException, Exception) as e:
                    print(f"[{timestamp}] Výpadek spojení ({e}). Zapisuji NaN a obnovuji socket...", flush=True)
                    modbus_client.close() 
                    for _ in sensors:
                        csv_row.append("NaN")

                # 3. Zápis do CSV (proběhne VŽDY)
                writer.writerow(csv_row)
                file.flush() 

                # 4. Zápis do INFLUXDB (proběhne JEN když jsou platná data)
                if data_valid:
                    # Založíme datový bod (tzv. measurement)
                    point = influxdb_client.Point("aqm_environment")
                    
                    # Projdeme senzory a naplníme bod reálnými daty
                    for i, s in enumerate(sensors):
                        field_name = s['name'].replace(f"{target_name}_", '')
                        value = csv_row[i + 1] # +1 protože na indexu 0 je timestamp
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
            modbus_client.close()
            influx_client.close() # Zavření Influx spojení

if __name__ == "__main__":
    main()