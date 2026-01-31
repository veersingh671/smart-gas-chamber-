# ESP32 Gas / Vacuum Chamber Controller

##  Project Overview
This project is a **custom ESP32-based gas/vacuum chamber control system** built on a fully custom-designed development board. The board integrates power regulation, relay control, and sensor interfaces, making it a **standalone, industrial-ready solution** for real-time environmental monitoring and control.
The system continuously monitors **pressure, oxygen concentration, temperature, and humidity** inside a chamber and automatically or manually controls external devices such as **vacuum pumps or gas valves**.

##  What This Project Does

* Measures chamber pressure using an **industrial 4–20mA pressure sensor**
* Converts current signal to accurate pressure values using calibrated ADC logic
* Monitors **oxygen (O₂) concentration (%)** in real time
* Measures **temperature and humidity** using DHT22
* Displays real-time sensor data on a **16×4 I²C LCD**
* Allows users to **set target pressure** using push buttons
* Automatically **turns relay ON/OFF** based on actual vs set pressure
* Provides **audible feedback** using an onboard buzzer
* Uses **non-blocking firmware design** for smooth real-time operation
* Supports **serial monitoring** for debugging and testing

##  Hardware Highlights

* Custom-designed **ESP32-WROOM-32 development board**
* Onboard **9V → 5V power regulation**
* Stable **3.3V supply** for ESP32 and sensors
* Integrated **relay driver circuit**
* Dedicated **ADC inputs** for analog sensors
* I²C interface for LCD (SDA: 21, SCL: 22)
* Push buttons for UI navigation (UP / DOWN / OK / BACK)
* Onboard buzzer for user feedback
* Compact, standalone, and deployment-ready design

---

##  What You Learn From This Project

###  Embedded Systems & ESP32

* ESP32 GPIO configuration and pin safety
* ADC usage and voltage/current conversion
* Handling multiple sensors simultaneously
* Designing **non-blocking firmware** using `millis()`

###  Sensor Interfacing

* Interfacing **4–20mA industrial sensors**
* Using shunt resistors for current measurement
* Oxygen sensor calibration and linear scaling
* Digital sensor handling (DHT22)
* Sensor data averaging and noise reduction

###  Control Systems

* Closed-loop pressure control logic
* Automatic vs manual relay control
* Threshold-based decision making
* Safety-oriented logic for chamber systems

###  Hardware Design

* Designing a **custom ESP32 development board**
* Power regulation (9V → 5V → 3.3V)
* Relay isolation and protection techniques
* Analog signal conditioning for ADC accuracy
* Industrial sensor compatibility

### User Interface & UX

* Menu-driven UI using push buttons
* Real-time LCD updates
* Audible feedback for better usability
* State-machine-based screen navigation

### Software Engineering Practices

* Modular and readable firmware architecture
* Well-documented source code
* Debugging using Serial Monitor
* Calibration constants and configuration handling
* Scalable design for future expansion

##  Applications & Use Cases

* Gas and vacuum chambers
* Laboratory automation systems
* Environmental monitoring
* Industrial process control
* Research and development setups
* Embedded systems learning and prototyping


##  Future Improvements

* Data logging to SD card
* Wi-Fi or Bluetooth monitoring dashboard
* Mobile or web-based UI
* Alarm and safety shutdown logic
* PID-based pressure control
* Cloud data storage and analytics


## 📄 License

This project is open for **educational, personal, and research use**. Feel free to modify and extend it as needed.


##  Contributions

I want you to know that contributions, suggestions, and improvements are welcome. Please feel free to fix the repository and submit a pull request.
