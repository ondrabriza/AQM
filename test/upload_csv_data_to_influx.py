import csv
from datetime import datetime, timedelta
import influxdb_client
from influxdb_client.client.write_api import SYNCHRONOUS

# ================= INFLUXDB SETTINGS =================
INFLUX_URL = "http://aqm-rpi.local:8086" 
INFLUX_TOKEN = "MgHdTrItUtbh8Dasbln-uATVlDVuymvMrr_hJZ_09cbPrhg3CaHNcv3vI-SJ2pB7CxqtTJouKs6g4G51v0rPHg=="
INFLUX_ORG = "AQM"
INFLUX_BUCKET = "brno_arboretum" 
MEASUREMENT_NAME = "brno_arboretum"
CSV_FILE_PATH = "csv_data/aqm_data_2026_05_13.csv" 
# ============================================

# ================= FILTRACE A POSUN ==================
# Časový úsek z CSV, který chceme vyfiltrovat (před posunem)
START_TIME = datetime.strptime("2026-05-13 10:45:42", "%Y-%m-%d %H:%M:%S")
END_TIME = datetime.strptime("2026-05-13 11:23:30", "%Y-%m-%d %H:%M:%S")

# Korekce času: posuneme zaznamenaný čas o +3 hodiny a 30 minut,
# abychom se dostali na reálných 14:15 - 14:40
TIME_SHIFT = timedelta(hours=3, minutes=30)
# =====================================================

def main():
    print(f"Připojování k InfluxDB na {INFLUX_URL}...")
    client = influxdb_client.InfluxDBClient(url=INFLUX_URL, token=INFLUX_TOKEN, org=INFLUX_ORG)
    write_api = client.write_api(write_options=SYNCHRONOUS) 

    points = []
    print(f"Načítám a zpracovávám CSV soubor: {CSV_FILE_PATH}")
    
    try:
        with open(CSV_FILE_PATH, mode='r', encoding='utf-8') as file:
            cleaned_file = (line.replace('\0', '') for line in file)
            reader = csv.DictReader(cleaned_file, delimiter=',') 
            
            for row_num, row in enumerate(reader, start=1):
                if any(val is not None and '\ufffd' in str(val) for val in row.values()):
                    continue

                try:
                    timestamp_str = row['Timestamp']
                    time_obj = datetime.strptime(timestamp_str, '%Y-%m-%d %H:%M:%S.%f')
                    
                    # 1. Vyfiltrování pouze chtěného úseku měření
                    if START_TIME <= time_obj <= END_TIME:
                        
                        # 2. Posunutí na reálný čas
                        real_time_obj = time_obj + TIME_SHIFT
                        
                        point = influxdb_client.Point(MEASUREMENT_NAME)
                        point.time(real_time_obj) # Zápis opraveného času
                        
                        for field_name, value in row.items():
                            if field_name == 'Timestamp':
                                continue 
                                
                            if value is None or value.strip() == '' or value.upper() == 'NAN':
                                continue 
                                
                            point.field(field_name, float(value))
                        
                        if len(point._fields) > 0:
                            points.append(point)
                    
                except ValueError as ve:
                    # Ignorujeme drobné chyby na poškozených řádcích
                    continue

        total_points = len(points)
        if total_points == 0:
            print("Nebyly nalezeny žádné záznamy v zadaném čase (nebo jsou všechna data nevalidní).")
            return

        print(f"Vyfiltrováno {total_points} záznamů a čas byl posunut. Odesílám...")
        write_api.write(bucket=INFLUX_BUCKET, org=INFLUX_ORG, record=points)
        print("✅ Upload úspěšně dokončen! Můžeš zkontrolovat data v Influx UI.")

    except FileNotFoundError:
        print(f"❌ CHYBA: Soubor '{CSV_FILE_PATH}' nebyl nalezen.")
    except Exception as e:
        print(f"❌ CHYBA: Nastala neočekávaná událost: {e}")
    finally:
        client.close()

if __name__ == "__main__":
    main()