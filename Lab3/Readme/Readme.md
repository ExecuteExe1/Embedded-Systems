# System Overview
 -This project implements a UART-controlled embedded monitoring system for temperature and humidity, featuring user authentication, dynamic configuration, multiple operating modes, safety mechanisms, and real-time interaction.

## 🔐 1.System Startup & Authentication
  1.Upon system startup, the user is prompted via UART to enter a password.
  2.Incorrect password:
   - An error message is displayed.
   - The user is prompted to re-enter the password.
  3.Correct password:
   - The system proceeds to request the user’s AEM (Student ID Number).

## 2.Menu Initialization (UART Interface)
 -After successful authentication, a UART-based menu is initialized.

 ### Available UART Commands:
 1.a → Increase sensor reading & printing frequency by 1 second  
  - Minimum allowed period: 2 seconds
  
2.b → Decrease sensor reading & printing frequency by 1 second
  - Maximum allowed period: 10 seconds
  
3.c → Change display mode:
  - Temperature only
  - Humidity only
  - Temperature & Humidity
  
4.d → Print:
  - Latest temperature and humidity values
  - Current system status

## 3.Operating Modes (Button-Controlled)
 The system supports two operating modes, which can be switched using a physical button.

 3.1 Mode A – Normal Monitoring
  - The system reads and prints temperature and humidity according to the user-defined frequency.
  - No LED behavior is triggered.

🔴 3.2 Mode B – Alert Monitoring
  - The system continues reading and printing sensor values.
  - LED behavior:
    - If Temperature > 25°C OR Humidity > 60%, the LED starts blinking every 1 second.
    - If values return to normal:
      - The LED continues blinking for the next 5 readings/printings.
      - Afterward, the LED turns off.

## 4.Dynamic Frequency Adjustment
 Every third button press, the system dynamically updates the reading/printing frequency.
 The new frequency is calculated as the sum of the last two digits of the AEM.
 ### Example:
  - If AEM = 10852 → Last two digits: 5 + 2 = 7
  - ➡️ New frequency = 7 seconds

## 5.UART Status Command
  When the command status is sent via UART, the system prints:
   - {Current Mode}, {Last Temperature & Humidity Values}, {Number of Mode Switches}
This provides a concise snapshot of the system’s current state.

## 6.Panic Reset Mechanism
 For safety reasons, the system includes a panic reset feature:
  - If Temperature > 35°C AND Humidity > 80%
  - For 3 consecutive readings:
    - A software reset is triggered.
    - A warning message is printed via UART before the reset occurs.
