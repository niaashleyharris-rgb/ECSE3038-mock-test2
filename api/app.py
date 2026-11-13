import os
from typing import Optional
from fastapi import FastAPI, Header, HTTPException, status
from pydantic import BaseModel
from pymongo import MongoClient
from dotenv import load_dotenv

# 1. Load Environment Variables from .env
load_dotenv()

MONGO_URL = os.getenv("MONGO_URL")
EXPECTED_API_KEY = os.getenv("API_KEY")

if not MONGO_URL:
    raise RuntimeError("MONGO_URL is missing from your .env file!")
if not EXPECTED_API_KEY:
    raise RuntimeError("API_KEY is missing from your .env file!")

# 2. Connect to MongoDB Atlas
client = MongoClient(MONGO_URL)
db = client["cold_room_db"]
readings_col = db["readings"]

app = FastAPI(title="Cold-Room Monitor API (Mock Test 2)")

# 3. Pydantic Models for Automatic Data Validation (Returns 422 on bad data)
class SensorReading(BaseModel):
    device_id: str
    temperature: float
    humidity: float
    timestamp: float

class AlarmLimit(BaseModel):
    threshold: float

# 4. Helper Function: Verify Custom API Key Header
def verify_api_key(x_api_key: Optional[str] = Header(None)):
    if not x_api_key:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="API Key header missing (X-API-Key required)"
        )
    if x_api_key != EXPECTED_API_KEY:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Invalid API Key provided"
        )
    return x_api_key

# --- Endpoints ---

@app.get("/")
def root():
    return {"status": "Cold-Room Monitor API is online"}

# POST /readings: Save new sensor data to MongoDB
@app.post("/readings", status_code=status.HTTP_201_CREATED)
def create_reading(reading: SensorReading, x_api_key: Optional[str] = Header(None)):
    verify_api_key(x_api_key)
    
    doc = reading.dict()
    readings_col.insert_one(doc)
    doc.pop("_id", None)  # Remove BSON ObjectId before returning JSON
    return {"message": "Reading recorded successfully", "data": doc}

# GET /readings: Retrieve sensor data (supports ?since= timestamp query)
@app.get("/readings")
def get_readings(since: Optional[float] = None, x_api_key: Optional[str] = Header(None)):
    verify_api_key(x_api_key)
    
    query = {}
    if since is not None:
        query["timestamp"] = {"$gte": since}
        
    readings = list(readings_col.find(query, {"_id": 0}))
    return readings

# PUT /alarm: Update temperature threshold setpoint
@app.put("/alarm")
def update_alarm(limit: AlarmLimit, x_api_key: Optional[str] = Header(None)):
    verify_api_key(x_api_key)
    
    return {"message": "Alarm setpoint updated", "threshold": limit.threshold}
