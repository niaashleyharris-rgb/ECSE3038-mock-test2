import os
import time
import secrets
from typing import Optional
from fastapi import FastAPI, Header, HTTPException, Response, status
from pydantic import BaseModel, Field
from pymongo import MongoClient
from dotenv import load_dotenv
import paho.mqtt.client as mqtt

load_dotenv()

MONGODB_URI = os.getenv("MONGODB_URI") 
MQTT_NAME = os.getenv("MQTT_NAME", "student")

if not MONGODB_URI:
    raise RuntimeError("MONGODB_URI is missing from .env")

client = MongoClient(MONGODB_URI)
db = client["cold_room_db"]
devices_col = db["devices"]
readings_col = db["readings"]
alarms_col = db["alarms"]

app = FastAPI(title="Cold-Room Monitor API")

# Connect to Class MQTT Broker (Slide 3)
MQTT_BROKER = "135.232.203.144"
mqtt_client = mqtt.Client()
try:
    mqtt_client.connect(MQTT_BROKER, 1883, 60)
    mqtt_client.loop_start()
except Exception as e:
    print(f"MQTT connection warning: {e}")

# --- Pydantic Models ---
class DeviceCreate(BaseModel):
    mac: str
    room: str

class ReadingCreate(BaseModel):
    mac: str
    temp: float = Field(ge=-40, le=80)  # Slide 7: 422 if outside DHT22 range

class AlarmUpdate(BaseModel):
    max_temp: float

# --- Routes ---

# A1: Register device & return generated key
@app.post("/devices", status_code=status.HTTP_201_CREATED)
def create_device(device: DeviceCreate):
    if devices_col.find_one({"mac": device.mac}):
        raise HTTPException(status_code=409, detail="MAC already registered")
    
    key = secrets.token_urlsafe(16)
    devices_col.insert_one({
        "mac": device.mac,
        "room": device.room,
        "api_key": key
    })
    return {"mac": device.mac, "room": device.room, "api_key": key}

# Slide 6: GET /devices must NEVER show the keys
@app.get("/devices")
def get_devices():
    return list(devices_col.find({}, {"_id": 0, "api_key": 0}))

# A2: Ingest reading with strict 401 / 403 / 422 checks
@app.post("/readings", status_code=status.HTTP_201_CREATED)
def create_reading(reading: ReadingCreate, x_api_key: str | None = Header(default=None)):
    if not x_api_key:
        raise HTTPException(status_code=401, detail="Missing X-API-Key header")
    
    device = devices_col.find_one({"api_key": x_api_key})
    if not device:
        raise HTTPException(status_code=401, detail="Invalid API key")
    
    if device["mac"] != reading.mac:
        raise HTTPException(status_code=403, detail="API key belongs to another MAC")
    
    doc = {
        "mac": reading.mac,
        "temp": reading.temp,
        "timestamp": time.time()
    }
    readings_col.insert_one(doc)
    doc.pop("_id", None)
    return doc

# A3: Retrieve readings newest first, with ?mac= and ?since= filters
@app.get("/readings")
def get_readings(mac: Optional[str] = None, since: Optional[float] = None):
    query = {}
    if mac:
        query["mac"] = mac
    if since is not None:
        query["timestamp"] = {"$gte": since}
        
    return list(readings_col.find(query, {"_id": 0}).sort("timestamp", -1))

# A4: Store alarm limit (201 new, 200 replaced) & publish retained MQTT
@app.put("/alarm/{mac}")
def set_alarm(mac: str, alarm: AlarmUpdate, response: Response):
    if not devices_col.find_one({"mac": mac}):
        raise HTTPException(status_code=404, detail="Unknown MAC address")
    
    existing = alarms_col.find_one({"mac": mac})
    alarms_col.update_one(
        {"mac": mac},
        {"$set": {"mac": mac, "max_temp": alarm.max_temp}},
        upsert=True
    )
    
    response.status_code = status.HTTP_200_OK if existing else status.HTTP_201_CREATED
    
    # Publish retained limit to ecse3038/<name>/alarm/max
    topic = f"ecse3038/{MQTT_NAME}/alarm/max"
    mqtt_client.publish(topic, str(alarm.max_temp), retain=True)
    
    return {"mac": mac, "max_temp": alarm.max_temp}