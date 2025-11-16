/*
 * AERAS Rickshaw Side - GPS SIMULATED VERSION (FIXED)
 * Added: Web app synchronization via /ride/pending endpoint
 * Minimal changes to original code
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>

// ===== OLED Display =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

// ===== WiFi Configuration =====
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";
const char* BACKEND_URL = "http://10.172.129.95:3000/api";

// ===== Rickshaw Info =====
String rickshawID = "RICK001";
String pullerName = "Abdul Karim";
bool isOnline = true;
int totalPoints = 0;

// ===== GPS Simulation =====
struct Location {
  double lat;
  double lng;
  String name;
};

Location locations[] = {
  {22.4633, 91.9714, "CUET_CAMPUS"},
  {22.4725, 91.9845, "PAHARTOLI"},
  {22.4580, 91.9920, "NOAPARA"},
  {22.4520, 91.9650, "RAOJAN"}
};

double currentLat = 22.4633;
double currentLng = 91.9714;

// ===== Active ride info =====
String currentRideID = "";
String pickupLocation = "";
String destinationLocation = "";
bool onActiveRide = false;
bool pickupConfirmed = false;

// Simulated movement
Location targetLocation;
double speedKmPerHour = 15.0;
unsigned long lastMoveTime = 0;
unsigned long lastLocationUpdate = 0;
unsigned long lastRideCheck = 0;
unsigned long lastStatusCheck = 0;  // NEW: For checking accepted status

// ===== Helper Functions =====
void displayMessage(String line1, String line2, String line3 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 10);
  display.println("AERAS SYSTEM");
  display.println("================");
  display.setCursor(0, 28);
  display.println(line1);
  display.setCursor(0, 40);
  display.println(line2);
  if (line3 != "") {
    display.setCursor(0, 52);
    display.println(line3);
  }
  display.display();
}

double calculateDistance(double lat1, double lon1, double lat2, double lon2) {
  double R = 6371000.0;
  double dLat = (lat2 - lat1) * PI / 180.0;
  double dLon = (lon2 - lon1) * PI / 180.0;
  
  double a = sin(dLat/2) * sin(dLat/2) +
             cos(lat1 * PI / 180.0) * cos(lat2 * PI / 180.0) *
             sin(dLon/2) * sin(dLon/2);
  
  double c = 2 * atan2(sqrt(a), sqrt(1-a));
  return R * c;
}

void setTargetLocation(String locationName) {
  locationName.toUpperCase();
  
  Serial.println("Searching for location: " + locationName);
  
  for (int i = 0; i < 4; i++) {
    bool match = false;
    
    if (locations[i].name == locationName) {
      match = true;
    }
    else if (locations[i].name.indexOf(locationName) >= 0) {
      match = true;
    }
    else if (locationName.indexOf("PAHARTOLI") >= 0 && locations[i].name == "PAHARTOLI") {
      match = true;
    }
    else if (locationName.indexOf("CUET") >= 0 && locations[i].name == "CUET_CAMPUS") {
      match = true;
    }
    else if (locationName.indexOf("NOAPARA") >= 0 && locations[i].name == "NOAPARA") {
      match = true;
    }
    else if (locationName.indexOf("RAOJAN") >= 0 && locations[i].name == "RAOJAN") {
      match = true;
    }
    
    if (match) {
      targetLocation = locations[i];
      Serial.println("✓ Target set: " + targetLocation.name);
      Serial.println("  Coords: " + String(targetLocation.lat, 6) + ", " + String(targetLocation.lng, 6));
      
      double dist = calculateDistance(currentLat, currentLng, targetLocation.lat, targetLocation.lng);
      Serial.println("  Distance: " + String(dist, 1) + " m");
      return;
    }
  }
  
  Serial.println("✗ Location not found, trying partial match...");
  
  if (locationName.indexOf("PAHAR") >= 0 || locationName.indexOf("Pahar") >= 0) {
    targetLocation = locations[1];
    Serial.println("✓ Matched to PAHARTOLI");
  } else {
    Serial.println("✗ Could not find location: " + locationName);
  }
}

double calculateBearing(double lat1, double lon1, double lat2, double lon2) {
  double dLon = (lon2 - lon1) * PI / 180.0;
  lat1 = lat1 * PI / 180.0;
  lat2 = lat2 * PI / 180.0;
  
  double y = sin(dLon) * cos(lat2);
  double x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dLon);
  double bearing = atan2(y, x) * 180.0 / PI;
  
  return fmod((bearing + 360.0), 360.0);
}

void displayStatus(String status, String message) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 10);
  display.println("AERAS Rickshaw");
  display.println("================");
  display.print("Status: ");
  display.println(status);
  display.println(message);
  display.print("Points: ");
  display.println(totalPoints);
  display.display();
}

// ===== Register Rickshaw =====
void registerRickshaw() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  HTTPClient http;
  String url = String(BACKEND_URL) + "/rickshaw/register";
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  String payload = "{";
  payload += "\"rickshawID\":\"" + rickshawID + "\",";
  payload += "\"pullerName\":\"" + pullerName + "\",";
  payload += "\"phoneNumber\":\"01712345678\",";
  payload += "\"currentLat\":" + String(currentLat, 6) + ",";
  payload += "\"currentLng\":" + String(currentLng, 6);
  payload += "}";
  
  int httpCode = http.POST(payload);
  if (httpCode > 0) {
    Serial.println("✓ Registered with backend");
  }
  
  http.end();
}

// ===== NEW: Check if web app accepted a ride =====
void checkWebAppAcceptance() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (onActiveRide) return;  // Already on a ride
  if (currentRideID == "") return;  // No pending ride to check
  if (millis() - lastStatusCheck < 2000) return;  // Check every 2 seconds
  
  lastStatusCheck = millis();
  
  HTTPClient http;
  String url = String(BACKEND_URL) + "/admin/rides?limit=10";
  
  http.begin(url);
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String response = http.getString();
    
    // Only look for OUR current pending ride ID
    String searchString = "\"rideID\":" + currentRideID;
    int ridePos = response.indexOf(searchString);
    
    if (ridePos > 0) {
      // Found our ride, now check its status
      int statusPos = response.indexOf("\"status\":\"", ridePos);
      if (statusPos > 0 && statusPos < ridePos + 500) {  // Within same ride object
        int statusStart = statusPos + 10;
        int statusEnd = response.indexOf("\"", statusStart);
        String status = response.substring(statusStart, statusEnd);
        
        // Check if assigned to us
        int rickshawPos = response.indexOf("\"rickshawID\":\"", ridePos);
        if (rickshawPos > 0 && rickshawPos < ridePos + 500) {
          int rickStart = rickshawPos + 14;
          int rickEnd = response.indexOf("\"", rickStart);
          String assignedRick = response.substring(rickStart, rickEnd);
          
          if (assignedRick == rickshawID && status == "ACCEPTED") {
            // Web app accepted! Extract ride details if we don't have them
            if (pickupLocation == "") {
              int pickupPos = response.indexOf("\"pickupBlock\":\"", ridePos);
              int pickupStart = pickupPos + 15;
              int pickupEnd = response.indexOf("\"", pickupStart);
              pickupLocation = response.substring(pickupStart, pickupEnd);
            }
            
            if (destinationLocation == "") {
              int destPos = response.indexOf("\"destination\":\"", ridePos);
              int destStart = destPos + 15;
              int destEnd = response.indexOf("\"", destStart);
              destinationLocation = response.substring(destStart, destEnd);
            }
            
            Serial.println("\n🌐 WEB APP ACCEPTED RIDE!");
            Serial.println("   Ride ID: " + currentRideID);
            Serial.println("   Pickup: " + pickupLocation);
            Serial.println("   Destination: " + destinationLocation);
            
            onActiveRide = true;
            pickupConfirmed = false;
            
            setTargetLocation(pickupLocation);
            
            displayMessage("Web Accepted!", "Going to pickup", pickupLocation);
            delay(2000);
            
            return;
          }
        }
      }
    }
  }
  
  http.end();
}

// ===== NEW: Check for ride status updates (pickup/complete) =====
void checkRideStatusUpdates() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!onActiveRide) return;  // Only check when on active ride
  if (currentRideID == "") return;
  if (millis() - lastStatusCheck < 1500) return;  // Check every 1.5 seconds (faster!)
  
  lastStatusCheck = millis();
  
  HTTPClient http;
  String url = String(BACKEND_URL) + "/admin/rides?limit=10";
  
  http.begin(url);
  http.setTimeout(3000);
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String response = http.getString();
    
    // Look for our current ride
    String searchString = "\"rideID\":" + currentRideID;
    int ridePos = response.indexOf(searchString);
    
    if (ridePos > 0) {
      // Check status
      int statusPos = response.indexOf("\"status\":\"", ridePos);
      if (statusPos > 0 && statusPos < ridePos + 500) {
        int statusStart = statusPos + 10;
        int statusEnd = response.indexOf("\"", statusStart);
        String status = response.substring(statusStart, statusEnd);
        
        // Debug logging
        static String lastStatus = "";
        if (status != lastStatus) {
          Serial.println("Status changed: " + lastStatus + " -> " + status);
          lastStatus = status;
        }
        
        // NEW: Check if pickup was confirmed from web app
        if (status == "PICKUP" && !pickupConfirmed) {
          Serial.println("\n🌐 🌐 🌐 WEB APP CONFIRMED PICKUP! 🌐 🌐 🌐");
          pickupConfirmed = true;
          
          // Extract destination if we don't have it
          if (destinationLocation == "") {
            int destPos = response.indexOf("\"destination\":\"", ridePos);
            if (destPos > 0) {
              int destStart = destPos + 15;
              int destEnd = response.indexOf("\"", destStart);
              destinationLocation = response.substring(destStart, destEnd);
            }
          }
          
          Serial.println("🗺️ Setting navigation to DESTINATION...");
          Serial.println("   Destination: " + destinationLocation);
          setTargetLocation(destinationLocation);
          
          displayMessage("Web Pickup OK", "Going to dest", destinationLocation);
          delay(2000);
          
          Serial.println("\n🚗 DRIVING TO DESTINATION...\n");
        }
        // Check if ride was completed from web app
        else if (status == "COMPLETED" && onActiveRide) {
          Serial.println("\n🌐 🌐 🌐 WEB APP COMPLETED RIDE! 🌐 🌐 🌐");
          Serial.println("   Resetting system...");
          
          onActiveRide = false;
          pickupConfirmed = false;
          currentRideID = "";
          pickupLocation = "";
          destinationLocation = "";
          
          displayStatus("AVAILABLE", "Waiting for rides");
          Serial.println("✓ System reset - Ready for new rides\n");
        }
      }
    } else {
      Serial.println("⚠️ Could not find ride " + currentRideID + " in response");
    }
  } else {
    Serial.println("✗ HTTP Error checking status: " + String(httpCode));
  }
  
  http.end();
}

// ===== Check for Ride Requests (using /ride/pending) =====
void checkForRideRequests() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastRideCheck < 3000) return;
  lastRideCheck = millis();
  
  HTTPClient http;
  String url = String(BACKEND_URL) + "/ride/pending?rickshawID=" + rickshawID;
  
  http.begin(url);
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String response = http.getString();
    
    if (response.indexOf("\"rides\":[") > 0 && response.indexOf("\"rideID\":") > 0) {
      int rideIDStart = response.indexOf("\"rideID\":") + 9;
      int rideIDEnd = response.indexOf(",", rideIDStart);
      String rideID = response.substring(rideIDStart, rideIDEnd);
      
      if (rideID != currentRideID || currentRideID == "") {
        
        int pickupStart = response.indexOf("\"pickupBlock\":\"") + 15;
        int pickupEnd = response.indexOf("\"", pickupStart);
        String pickup = response.substring(pickupStart, pickupEnd);
        
        int destStart = response.indexOf("\"destination\":\"") + 15;
        int destEnd = response.indexOf("\"", destStart);
        String dest = response.substring(destStart, destEnd);
        
        int distStart = response.indexOf("\"distance\":\"") + 12;
        int distEnd = response.indexOf("\"", distStart);
        String distance = response.substring(distStart, distEnd);
        
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println("NEW RIDE REQUEST");
        display.println("================");
        
        display.print("Pickup: ");
        display.println(pickup);
        
        display.print("Dest: ");
        display.println(dest);
        
        display.print("Distance: ");
        display.print(distance);
        display.println(" km");
        
        display.print("Est.Points: ");
        float dist = distance.toFloat();
        if (dist <= 2) display.println("10");
        else if (dist <= 5) display.println("8-10");
        else display.println("5-10");
        
        display.println("");
        display.println("ACCEPT or REJECT?");
        display.display();
        
        Serial.println("\n📢 📢 📢 NEW RIDE REQUEST! 📢 📢 📢");
        Serial.println("Ride ID: " + rideID);
        Serial.println("Pickup: " + pickup + " → Destination: " + dest);
        Serial.println("Distance: " + distance + " km");
        Serial.println("=====================================");
        Serial.println("Type 'ACCEPT' to accept this ride");
        Serial.println("Type 'REJECT' to reject this ride");
        Serial.println("=====================================\n");
        
        currentRideID = rideID;
        pickupLocation = pickup;
        destinationLocation = dest;
      }
    }
  }
  
  http.end();
}

// ===== Accept Ride =====
void acceptRide() {
  if (currentRideID == "" || onActiveRide) {
    Serial.println("✗ No ride to accept or already on ride");
    return;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    displayMessage("WiFi Error", "Cannot accept");
    return;
  }
  
  HTTPClient http;
  String url = String(BACKEND_URL) + "/ride/accept";
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);  // 5 second timeout
  
  String payload = "{";
  payload += "\"rideID\":" + currentRideID + ",";
  payload += "\"rickshawID\":\"" + rickshawID + "\"";
  payload += "}";
  
  Serial.println("\n🤝 Accepting ride " + currentRideID + "...");
  int httpCode = http.POST(payload);
  
  if (httpCode == 200) {
    String response = http.getString();
    
    if (response.indexOf("\"success\":true") > 0) {
      Serial.println("✓ ✓ ✓ RIDE ACCEPTED! ✓ ✓ ✓");
      Serial.println("Pickup: " + pickupLocation);
      Serial.println("Destination: " + destinationLocation);
      
      onActiveRide = true;
      pickupConfirmed = false;
      
      Serial.println("\n🚗 Setting navigation to PICKUP location...");
      setTargetLocation(pickupLocation);
      
      displayMessage("Ride Accepted!", "Going to pickup");
      delay(2000);
      
      Serial.println("\n🗺️ NAVIGATION STARTED - Moving to pickup...\n");
    } else {
      Serial.println("✗ Ride already taken by another puller");
      displayMessage("Ride Taken", "Try another");
      delay(2000);
      currentRideID = "";
      displayStatus("AVAILABLE", "Waiting for rides");
    }
  } else {
    Serial.println("✗ HTTP Error: " + String(httpCode));
    displayMessage("Accept Failed", "Try again");
    delay(2000);
  }
  
  http.end();
}

// ===== Confirm Pickup =====
void confirmPickup() {
  if (!onActiveRide || pickupConfirmed) {
    Serial.println("✗ Not at pickup or already confirmed");
    return;
  }
  
  double distanceToPickup = calculateDistance(
    currentLat, currentLng,
    targetLocation.lat, targetLocation.lng
  );
  
  Serial.println("\n📍 Checking pickup location...");
  Serial.println("   Distance to pickup: " + String(distanceToPickup, 1) + " m");
  
  if (distanceToPickup > 100) {
    Serial.println("✗ TOO FAR from pickup location!");
    Serial.println("   You must be within 100m to confirm pickup");
    Serial.println("   Current distance: " + String(distanceToPickup, 1) + " m");
    displayMessage("Too Far!", "Distance: " + String((int)distanceToPickup) + "m");
    delay(2000);
    return;
  }
  
  HTTPClient http;
  String url = String(BACKEND_URL) + "/ride/pickup";
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  String payload = "{\"rideID\":" + currentRideID + "}";
  
  int httpCode = http.POST(payload);
  
  if (httpCode == 200) {
    Serial.println("✓ ✓ ✓ PICKUP CONFIRMED! ✓ ✓ ✓");
    pickupConfirmed = true;
    
    Serial.println("\n🗺️ Setting navigation to DESTINATION...");
    Serial.println("   Destination: " + destinationLocation);
    setTargetLocation(destinationLocation);
    
    displayMessage("Pickup OK", "Going to dest");
    delay(2000);
    
    Serial.println("\n🚗 DRIVING TO DESTINATION...\n");
  }
  
  http.end();
}

// ===== Complete Ride =====
void completeRide() {
  if (!onActiveRide || !pickupConfirmed) {
    Serial.println("✗ Cannot complete - not on active ride");
    return;
  }
  
  double distanceToTarget = calculateDistance(
    currentLat, currentLng,
    targetLocation.lat, targetLocation.lng
  );
  
  Serial.println("Distance to destination: " + String(distanceToTarget, 2) + " m");
  
  if (distanceToTarget > 100) {
    Serial.println("✗ TOO FAR from destination!");
    Serial.println("  Current: " + String(currentLat, 6) + ", " + String(currentLng, 6));
    Serial.println("  Target: " + String(targetLocation.lat, 6) + ", " + String(targetLocation.lng, 6));
    Serial.println("  Must be within 100m for auto-approval");
    displayMessage("Too Far!", "Distance: " + String((int)distanceToTarget) + "m");
    delay(3000);
    return;
  }
  
  HTTPClient http;
  String url = String(BACKEND_URL) + "/ride/complete";
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  String payload = "{";
  payload += "\"rideID\":" + currentRideID + ",";
  payload += "\"dropLat\":" + String(currentLat, 6) + ",";
  payload += "\"dropLng\":" + String(currentLng, 6);
  payload += "}";
  
  Serial.println("Completing ride with drop location:");
  Serial.println("  Lat: " + String(currentLat, 6));
  Serial.println("  Lng: " + String(currentLng, 6));
  
  int httpCode = http.POST(payload);
  
  if (httpCode == 200) {
    String response = http.getString();
    Serial.println("Response: " + response);
    
    int pointsStart = response.indexOf("\"points\":") + 9;
    int pointsEnd = response.indexOf(",", pointsStart);
    if (pointsEnd == -1) pointsEnd = response.indexOf("}", pointsStart);
    int pointsEarned = response.substring(pointsStart, pointsEnd).toInt();
    
    int distStart = response.indexOf("\"distance\":\"") + 12;
    int distEnd = response.indexOf("\"", distStart);
    String dropDist = response.substring(distStart, distEnd);
    
    String status = "COMPLETED";
    if (response.indexOf("\"PENDING_REVIEW\"") > 0) {
      status = "PENDING_REVIEW";
    }
    
    totalPoints += pointsEarned;
    
    Serial.println("\n✓ RIDE COMPLETED!");
    Serial.println("  Status: " + status);
    Serial.println("  Points Earned: " + String(pointsEarned));
    Serial.println("  Drop Distance: " + dropDist + " m");
    Serial.println("  Total Points: " + String(totalPoints));
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("RIDE COMPLETED!");
    display.println("================");
    
    if (pointsEarned == 10) {
      display.println("PERFECT DROP!");
    } else if (pointsEarned >= 8) {
      display.println("GREAT DROP!");
    } else if (pointsEarned >= 5) {
      display.println("GOOD DROP");
    } else if (pointsEarned > 0) {
      display.println("COMPLETED");
    } else {
      display.println("UNDER REVIEW");
    }
    
    display.println("");
    display.print("Points: +");
    display.println(pointsEarned);
    display.print("Distance: ");
    display.print(dropDist);
    display.println(" m");
    display.print("Total: ");
    display.println(totalPoints);
    display.println("");
    display.println("Resetting...");
    display.display();
    
    delay(5000);
    
    Serial.println("\n🔄 Resetting system for next ride...");
    onActiveRide = false;
    pickupConfirmed = false;
    currentRideID = "";
    pickupLocation = "";
    destinationLocation = "";
    
    displayStatus("AVAILABLE", "Waiting for rides");
    Serial.println("✓ System reset - Ready for new rides\n");
  } else {
    Serial.println("✗ HTTP Error: " + String(httpCode));
  }
  
  http.end();
}

// ===== GPS Movement Simulation =====
void simulateMovement() {
  if (!onActiveRide) return;
  
  if (millis() - lastMoveTime > 1000) {
    double distance = calculateDistance(currentLat, currentLng, targetLocation.lat, targetLocation.lng);
    
    if (distance > 5) {
      double bearing = calculateBearing(currentLat, currentLng, targetLocation.lat, targetLocation.lng);
      
      double metersPerSecond = (speedKmPerHour * 1000.0) / 3600.0;
      
      double latDegreesPerMeter = 1.0 / 111320.0;
      double lngDegreesPerMeter = 1.0 / (111320.0 * cos(currentLat * PI / 180.0));
      
      double bearingRad = bearing * PI / 180.0;
      double deltaLatMeters = metersPerSecond * cos(bearingRad);
      double deltaLngMeters = metersPerSecond * sin(bearingRad);
      
      currentLat += deltaLatMeters * latDegreesPerMeter;
      currentLng += deltaLngMeters * lngDegreesPerMeter;
      
      Serial.println("📍 Moving to " + targetLocation.name);
      Serial.println("   Distance: " + String(distance, 1) + " m");
      Serial.println("   Bearing: " + String((int)bearing) + "°");
      Serial.println("   Current: " + String(currentLat, 6) + ", " + String(currentLng, 6));
    } else {
      Serial.println("\n✓ ✓ ✓ ARRIVED at " + targetLocation.name + " ✓ ✓ ✓");
      Serial.println("   Final coords: " + String(currentLat, 6) + ", " + String(currentLng, 6));
      Serial.println("   Target coords: " + String(targetLocation.lat, 6) + ", " + String(targetLocation.lng, 6));
      Serial.println("   Distance: " + String(distance, 2) + " m");
      
      if (!pickupConfirmed) {
        displayMessage("At Pickup!", "Type: PICKUP");
        Serial.println("\n🎯 AT PICKUP LOCATION - Type 'PICKUP' to confirm\n");
      } else {
        displayMessage("At Destination!", "Type: COMPLETE");
        Serial.println("\n🏁 AT DESTINATION - Type 'COMPLETE' to finish ride\n");
      }
    }
    
    lastMoveTime = millis();
  }
}

// ===== Navigation Display =====
void updateNavigationDisplay() {
  if (!onActiveRide) return;
  
  double distance = calculateDistance(currentLat, currentLng, targetLocation.lat, targetLocation.lng);
  double bearing = calculateBearing(currentLat, currentLng, targetLocation.lat, targetLocation.lng);
  
  static unsigned long rideStartTime = 0;
  if (rideStartTime == 0) rideStartTime = millis();
  int rideDuration = (millis() - rideStartTime) / 1000;
  int minutes = rideDuration / 60;
  int seconds = rideDuration % 60;
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  
  if (!pickupConfirmed) {
    display.println(">> TO PICKUP <<");
  } else {
    display.println(">> TO DESTINATION <<");
  }
  
  display.println("================");
  
  display.print("Now: ");
  display.print(currentLat, 4);
  display.print(",");
  display.println(currentLng, 4);
  
  display.print("To: ");
  display.println(targetLocation.name);
  
  display.print("Dist: ");
  display.print((int)distance);
  display.print("m ");
  
  if (bearing >= 337.5 || bearing < 22.5) display.print("N");
  else if (bearing >= 22.5 && bearing < 67.5) display.print("NE");
  else if (bearing >= 67.5 && bearing < 112.5) display.print("E");
  else if (bearing >= 112.5 && bearing < 157.5) display.print("SE");
  else if (bearing >= 157.5 && bearing < 202.5) display.print("S");
  else if (bearing >= 202.5 && bearing < 247.5) display.print("SW");
  else if (bearing >= 247.5 && bearing < 292.5) display.print("W");
  else display.print("NW");
  
  display.println();
  
  display.print("Time: ");
  if (minutes > 0) {
    display.print(minutes);
    display.print("m ");
  }
  display.print(seconds);
  display.println("s");
  
  display.print("Est.Points: ");
  if (distance <= 50) display.println("8-10");
  else if (distance <= 100) display.println("5-8");
  else display.println("Review");
  
  display.display();
  
  if (distance <= 5) {
    rideStartTime = 0;
  }
}

// ===== Send Location Update =====
void sendLocationUpdate() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastLocationUpdate < 5000) return;
  lastLocationUpdate = millis();
  
  HTTPClient http;
  String url = String(BACKEND_URL) + "/rickshaw/location";
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  String payload = "{";
  payload += "\"rickshawID\":\"" + rickshawID + "\",";
  payload += "\"lat\":" + String(currentLat, 6) + ",";
  payload += "\"lng\":" + String(currentLng, 6);
  payload += "}";
  
  http.POST(payload);
  http.end();
}

// ===== Serial Commands =====
void handleSerialCommand() {
  String command = Serial.readStringUntil('\n');
  command.trim();
  command.toUpperCase();
  
  if (command == "ACCEPT") {
    acceptRide();
  }
  else if (command == "REJECT") {
    Serial.println("Ride rejected");
    currentRideID = "";
    displayStatus("AVAILABLE", "Waiting for rides");
  }
  else if (command == "PICKUP") {
    confirmPickup();
  }
  else if (command == "COMPLETE") {
    completeRide();
  }
  else if (command == "STATUS") {
    Serial.println("\n===== RICKSHAW STATUS =====");
    Serial.println("ID: " + rickshawID);
    Serial.println("Location: " + String(currentLat, 6) + ", " + String(currentLng, 6));
    Serial.println("Points: " + String(totalPoints));
    Serial.println("On Ride: " + String(onActiveRide ? "YES" : "NO"));
    if (onActiveRide) {
      Serial.println("Pickup Confirmed: " + String(pickupConfirmed ? "YES" : "NO"));
      Serial.println("Target: " + targetLocation.name);
      double dist = calculateDistance(currentLat, currentLng, targetLocation.lat, targetLocation.lng);
      Serial.println("Distance to target: " + String(dist, 1) + " m");
    }
    Serial.println("===========================\n");
  }
  else if (command == "HELP") {
    Serial.println("\n===== COMMANDS =====");
    Serial.println("ACCEPT   - Accept pending ride");
    Serial.println("REJECT   - Reject pending ride");
    Serial.println("PICKUP   - Confirm pickup");
    Serial.println("COMPLETE - Complete ride");
    Serial.println("STATUS   - Show status");
    Serial.println("====================\n");
  }
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== AERAS RICKSHAW SIDE ===");
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("✗ OLED failed"));
    for(;;);
  }
  
  displayMessage("Rickshaw System", "Initializing...");
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi Connected");
    Serial.println("IP: " + WiFi.localIP().toString());
    displayMessage("WiFi Connected", rickshawID);
    delay(2000);
  } else {
    Serial.println("\n✗ WiFi Failed");
    displayMessage("WiFi Error", "Offline Mode");
    delay(2000);
  }
  
  registerRickshaw();
  
  displayStatus("AVAILABLE", "Waiting for rides");
  Serial.println("\n=== Rickshaw " + rickshawID + " Ready ===");
  Serial.println("Location: " + String(currentLat, 6) + ", " + String(currentLng, 6));
  Serial.println("\n✅ WEB APP SYNC ENABLED");
  Serial.println("Hardware will detect web app acceptances automatically");
  Serial.println("\nCommands: ACCEPT, REJECT, PICKUP, COMPLETE, STATUS\n");
}

// ===== Main Loop =====
void loop() {
  sendLocationUpdate();
  
  if (!onActiveRide) {
    checkForRideRequests();
    checkWebAppAcceptance();  // Check if web app accepted
  } else {
    // Active ride - check for status updates from web app
    checkRideStatusUpdates();  // Check for pickup/complete from web app
    simulateMovement();
    updateNavigationDisplay();
    
    // Debug: Print current state every 5 seconds
    static unsigned long lastDebug = 0;
    if (millis() - lastDebug > 5000) {
      lastDebug = millis();
      Serial.println("\n--- STATUS ---");
      Serial.println("Ride ID: " + currentRideID);
      Serial.println("Pickup Confirmed: " + String(pickupConfirmed ? "YES" : "NO"));
      Serial.println("Target: " + targetLocation.name);
      Serial.println("Checking web app status...");
    }
  }
  
  if (Serial.available()) {
    handleSerialCommand();
  }
  
  delay(100);
}