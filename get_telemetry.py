import fastf1
import os

# Create a data directory if it doesn't exist
os.makedirs('data', exist_ok=True)

# Enable caching to speed up downloads
fastf1.Cache.enable_cache('data')  

print("Downloading Abu Dhabi 2021 Race Data...")
# Load the 2021 Abu Dhabi Race
session = fastf1.get_session(2021, 'Abu Dhabi', 'R')
session.load()

# Get all driver numbers from the session
drivers = session.drivers

for driver in drivers:
    driver_name = session.get_driver(driver)['Abbreviation']
    print(f"Extracting telemetry for {driver_name}...")
    
    laps = session.laps.pick_driver(driver)
    telemetry = laps.get_telemetry()
    
    clean_data = telemetry[['Speed', 'Throttle']]
    
    filename = f"data/{driver}.csv"
    clean_data.to_csv(filename, index=False)

print("All driver data extracted successfully!")
