# 🚴 Citi Bike for Pebble Watch

**Real-time NYC Citi Bike availability at your wrist**

![License](https://img.shields.io/badge/license-Apache%202.0-blue) ![Platform](https://img.shields.io/badge/platform-Pebble-lightblue) ![Status](https://img.shields.io/badge/status-active-brightgreen)

---

## 🎯 What is This?

A lightweight Pebble smartwatch application that puts NYC Citi Bike station information right on your wrist. Find nearby bike stations, check real-time availability, and see how many bikes (regular & e-bikes) and docks are available—all without pulling out your phone.

**Perfect for:** Commuters, cyclists, and anyone exploring NYC on two wheels 🗽🚲

---

## ✨ Key Features

- **📍 Location-Based Finder** — Automatically detects your location and finds nearby Citi Bike stations
- **🎨 Color-Coded Availability** — Quick visual feedback on bike availability
  - 🟢 **Green** = Plenty of bikes available
  - 🟡 **Yellow** = Low availability  
  - 🔴 **Red** = No bikes available
- **🚴 Dual Bike Types** — See counts for regular bikes AND e-bikes separately
- **📊 Dock Status** — Check available parking spots at a glance
- **⬅️➡️ Easy Navigation** — Scroll through nearby stations with watch buttons
- **⚠️ Distance Warnings** — Get alerted if the nearest station is far away
- **🔄 Live Data** — Fetches real-time data from NYC's official GBFS feed

---

## 📱 Supported Devices

Works on all Pebble watches:
- Pebble Time
- Pebble Time Round  
- Pebble Time Steel
- Pebble Sport
- Original Pebble (B&W)

---

## 🛠️ Build & Installation

### Requirements
- [Pebble SDK](https://developer.pebble.com/sdk/)
- Node.js / npm
- A Pebble developer account

### Steps

```bash
# Clone the repository
git clone <repo-url>
cd probable-pancake

# Install dependencies
npm install

# Build the app
npm run build

# Follow Pebble SDK instructions to load on your watch
```

---

## 🚀 How It Works

### Architecture

```
┌─────────────────────────────────────────────────────┐
│           Pebble Smartwatch (C)                      │
│  - Display layer with color-coded availability      │
│  - Button handlers for navigation                   │
│  - Location requests                                │
└──────────────────────┬──────────────────────────────┘
                       │ (Message protocol)
                       │
┌──────────────────────▼──────────────────────────────┐
│        Phone-Side Bridge (JavaScript)               │
│  - Fetches live GBFS data from NYC Citi Bike        │
│  - Geolocation lookup                               │
│  - Nearby station calculation (250m radius)         │
│  - Sends formatted data back to watch               │
└──────────────────────┬──────────────────────────────┘
                       │ (HTTP requests)
                       │
┌──────────────────────▼──────────────────────────────┐
│  NYC Citi Bike GBFS API (Cloud)                     │
│  https://gbfs.citibikenyc.com/gbfs/en/              │
│  - Station information (locations, names)           │
│  - Real-time status (bikes, docks available)        │
└─────────────────────────────────────────────────────┘
```

### Data Flow

1. **Launch** → Phone-side JS loads station database
2. **Button Press** → Watch requests location + nearest stations
3. **Geolocation** → Phone gets GPS coordinates
4. **Calculation** → Uses Haversine formula to find nearby stations (within 250m)
5. **Display** → Watch shows station name, bike count, e-bike count, available docks
6. **Navigation** → User scrolls through nearby stations with ← → buttons

---

## 🎮 Usage

### On Your Watch

1. **Launch** the Citi Bike app
2. **Watch loads** — Shows "Loading..." while fetching data
3. **See results** — Station name and bike availability displayed
4. **Navigate** — Use UP/DOWN buttons to browse nearby stations
5. **Toggle view** — SELECT button switches between bike count and dock count

### Button Controls

| Button | Action |
|--------|--------|
| UP ⬆️ | Previous station |
| DOWN ⬇️ | Next station |
| SELECT 🔲 | Toggle bikes/docks view |

---

## 📊 What You'll See

```
╔════════════════════════╗
║   GRAND CENTRAL        ║  ← Station name (trimmed to 40 chars)
║   Bikes: 12            ║  ← Total bike count (color-coded)
║   eBikes: 3            ║  ← E-bike count
║   Regular: 9           ║  ← Regular bikes
║   [2 of 8]             ║  ← Position in nearby list
║   Far: 342m / 4 min ⚠️  ║  ← Warning if out of range
╚════════════════════════╝
```

---

## ⚙️ Technical Details

### Constants

- **Search Radius:** 250 meters (adjustable in `src/js/app.js`)
- **Walking Speed:** 80 meters/minute (for distance estimates)
- **Geolocation Timeout:** 15 seconds
- **Location Cache:** 60 seconds

### Error Codes

| Code | Meaning |
|------|---------|
| 1 | Geolocation failed |
| 2 | No stations found within range |
| 3 | Live data unavailable |

### Message Keys

Communication between watch and phone uses these message keys:

```javascript
UseNearest (10001)  → Get nearest station
RequestNext (10005) → Navigate to next station
RequestPrev (10006) → Navigate to previous station
Bundle (10003)      ← Station data response
ErrorCode (10004)   ← Error response
```

---

## 🗺️ Data Sources

- **Station Information:** NYC Citi Bike GBFS API (`station_information.json`)
- **Real-Time Status:** NYC Citi Bike GBFS API (`station_status.json`)
- **Location:** Device GPS + phone geolocation API

---

## 🐛 Known Limitations

- Requires phone with internet connection for data fetch
- GPS accuracy varies; may find different stations than expected in dense areas
- Limited display space — long station names are truncated
- Geolocation may take 5-15 seconds depending on signal

---

## 📝 License

Licensed under the Apache License 2.0. See [LICENSE](LICENSE) file for details.

---

## 🤝 Contributing

Found a bug? Want to add a feature? Contributions welcome!

### Ideas for Enhancement
- Add favorite stations
- Show docking instructions for certain bikes
- Battery-friendly background location polling  
- Multi-city support
- Trip history tracking

---

## 👤 Author

**probable-pancake** — Built for Pebble watch enthusiasts who love NYC cycling

---

<div align="center">

### 🚴 Happy Biking! 🚴

*Get on your bike and ride* 🎵

</div>
