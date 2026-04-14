import os
import yaml
import time
import csv
from datetime import datetime
from pymodbus.client import ModbusTcpClient, ModbusSerialClient
from pymodbus.exceptions import ModbusException, ConnectionException # <-- Přidáno pro odchytávání výpadků

# ================= SETTINGS =================
YAML_FILE = 'config.yaml'
CSV_FILE = 'aqm_data.csv'
MEASUREMENT_INTERVAL = 1  # in seconds
MODBUS_MODE = "RTU"         # Options: "TCP" or "RTU"
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
        # 64-bit value requires 4 registers
        r0 = regs[idx]
        r1 = regs[idx + 1]
        r2 = regs[idx + 2]
        r3 = regs[idx + 3]
        if swap == 'word':
            # Obrácené pořadí slov (Little-endian words)
            raw_value = (r3 << 48) | (r2 << 32) | (r1 << 16) | r0
        else:
            # Standardní pořadí (Big-endian words)
            raw_value = (r0 << 48) | (r1 << 32) | (r2 << 16) | r3

    elif data_type == 'uint32':
        # 32-bit value requires 2 registers
        high = regs[idx]
        low = regs[idx + 1]
        if swap == 'word':
            raw_value = (low << 16) | high
        else:
            raw_value = (high << 16) | low
            
    elif data_type == 'int16':
        # 16-bit signed integer
        raw_value = regs[idx]
        if raw_value > 32767:
            raw_value -= 65536
            
    else:
        # Default fallback (např. uint16)
        raw_value = regs[idx]

    # Aplikace měřítka a zaokrouhlení
    result = raw_value * scale
    if precision == 0:
        return int(result)
    else:
        return round(result, precision)

def main():
    target_name = 'aqm_tcp' if MODBUS_MODE.upper() == 'TCP' else 'aqm_rtu'

    print("=================================")
    print(f"   Data Acquisition: {MODBUS_MODE}")
    print("=================================")
    print(f"Loading configuration for '{target_name}' from {YAML_FILE}...")
    
    try:
        device_config, sensors = load_configuration(target_name)
    except Exception as e:
        print(f"Configuration error: {e}")
        return

    min_address = min(s['address'] for s in sensors)
    max_address = max(s['address'] for s in sensors)
    register_count = (max_address - min_address) + 2 
    slave_id = device_config.get('slave', 1)

    print(f"Target block: address {min_address} to {min_address + register_count - 1} ({register_count} registers total)")
    print(f"Found {len(sensors)} sensors to log.")

    if MODBUS_MODE.upper() == 'TCP':
        client = ModbusTcpClient(
            host=device_config.get('host'), 
            port=device_config.get('port', 502), 
            timeout=2 # Mírně zvednutý timeout pro lepší stabilitu
        )
    else:
        client = ModbusSerialClient(
            port=device_config.get('port', 'COM8'),
            baudrate=device_config.get('baudrate', 115200),
            stopbits=device_config.get('stopbits', 1),
            bytesize=device_config.get('bytesize', 8),
            parity=device_config.get('parity', 'N'),
            timeout=2
        )

    print("Connecting...")
    client.connect() # Provedeme první pokus, ale nezastavíme skript, pokud selže rovnou.

    csv_header = ['Timestamp'] + [s['name'].replace(f"{target_name}_", '') for s in sensors]
    file_exists = os.path.isfile(CSV_FILE)

    with open(CSV_FILE, mode='a', newline='', encoding='utf-8') as file:
        writer = csv.writer(file, delimiter=';')
        
        if not file_exists:
            writer.writerow(csv_header)
            print(f"Created new CSV file: {CSV_FILE}")
        else:
            print(f"Appending to existing CSV file: {CSV_FILE}")

        print("Starting data acquisition (Press Ctrl+C to stop)...")

        try:
            while True:
                start_time = time.time()
                timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]
                csv_row = [timestamp]
                
                try:
                    # 1. Proaktivní kontrola TCP spojení (Serial to nepotřebuje tak akutně)
                    if MODBUS_MODE.upper() == 'TCP' and not client.is_socket_open():
                        client.connect()

                    # 2. Samotné čtení dat
                    result = client.read_input_registers(address=min_address, count=register_count, device_id=slave_id)
                    
                    if not result.isError():
                        regs = result.registers
                        
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
                        
                        # Vypisuje jen první a devátý senzor z pole (ujisti se, že jich máš tolik, jinak to hodí IndexError)
                        try:
                            print(f"[{timestamp}] OK -> {csv_header[1]}: {csv_row[1]} | {csv_header[9]}: {csv_row[9]}")
                        except IndexError:
                            print(f"[{timestamp}] OK -> Data uložena (méně než 9 senzorů pro výpis).")
                            
                    else:
                        # Chyba vyhodnocená knihovnou bez pádu (např. chyba adresy)
                        print(f"[{timestamp}] Chyba PDU nebo adresy. Zapisuji NaN.")
                        for _ in sensors:
                            csv_row.append("NaN")
                            
                except (ModbusException, ConnectionException, Exception) as e:
                    # Zde odchytíme kompletní výpadky, timeouty a odpojení ESP
                    print(f"[{timestamp}] Výpadek spojení ({e}). Zapisuji NaN a obnovuji socket...")
                    
                    # Nutné zavřít spojení, aby se mohlo v dalším cyklu vytvořit čisté nové
                    client.close() 
                    
                    for _ in sensors:
                        csv_row.append("NaN")

                # 3. Zápis do souboru (proběhne VŽDY, ať už s reálnými daty nebo s NaN)
                writer.writerow(csv_row)
                file.flush() 

                # 4. Časování smyčky
                elapsed = time.time() - start_time
                time.sleep(max(0, MEASUREMENT_INTERVAL - elapsed))

        except KeyboardInterrupt:
            print("\nAcquisition stopped by user. Data successfully appended to CSV.")
        finally:
            client.close()

if __name__ == "__main__":
    main()