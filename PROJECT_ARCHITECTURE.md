# ZMK Azoteq IQS5XX Driver Project Architecture

## Overview
This project is a ZMK (Zephyr Mechanical Keyboard) module that provides a driver for the Azoteq IQS5XX trackpad controller. It supports multi-touch gestures including single finger navigation, two-finger scrolling/zoom, three-finger gestures, and advanced drag-and-drop functionality similar to macOS trackpads.

## Core Components

### 1. Hardware Interface (`src/iqs5xx.c` + `include/iqs5xx.h`)
- **Purpose**: Low-level I2C communication with the IQS5XX chip
- **Key Features**:
  - I2C device communication at address 0x74
  - Interrupt-driven data ready handling via GPIO
  - Hardware gesture recognition (single tap, tap-and-hold, two-finger tap, swipes, zoom, scroll)
  - Multi-finger tracking (up to 5 fingers)
  - Coordinate transformation support (rotation, inversion)
  - Power management with idle detection

### 2. Gesture Processing System
The gesture system is modularized into separate files for maintainability:

#### Main Controller (`src/trackpad.c`)
- **Purpose**: Central event dispatcher and state management
- **Key Functions**:
  - `trackpad_trigger_handler()`: Main interrupt handler
  - Event rate limiting and idle detection
  - State coordination between different gesture modules
  - Input event dispatching to ZMK/Zephyr input subsystem

#### Single Finger Handler (`src/single_finger.c`)
- **Purpose**: Mouse cursor movement and basic dragging
- **Gestures Supported**:
  - `GESTURE_SINGLE_TAP`: Left mouse click
  - `GESTURE_TAP_AND_HOLD`: Drag start (button press and hold)
  - Cursor movement with sensitivity scaling
  - Drag continuation while button is held

#### Two Finger Handler (`src/two_finger.c`)
- **Purpose**: Advanced two-finger gestures
- **Gestures Supported**:
  - `GESTURE_TWO_FINGER_TAP`: Right mouse click
  - Pinch-to-zoom (zoom in/out commands)
  - Two-finger scrolling (vertical and horizontal)
  - Smart gesture detection with movement analysis

#### Three Finger Handler (`src/three_finger.c`)
- **Purpose**: System-level keyboard shortcuts
- **Gestures Supported**:
  - Three-finger tap: Mission Control (Cmd+F3)
  - Three-finger swipes: App switching and desktop navigation

### 3. Coordinate Transformation (`src/coordinate_transform.c`)
- **Purpose**: Hardware coordinate mapping and orientation handling
- **Features**:
  - 90°/180°/270° rotation support
  - X/Y axis inversion
  - Configurable via device tree

### 4. Keyboard Integration (`src/trackpad_keyboard_events.c`)
- **Purpose**: ZMK keyboard event generation
- **Features**:
  - HID keyboard report generation
  - Modifier key combinations
  - Integration with ZMK's endpoint system

## Hardware Gesture Detection

The IQS5XX chip provides hardware-level gesture recognition:

### Available Hardware Gestures
- **gestures0 (Single finger)**: 
  - `GESTURE_SINGLE_TAP` (0x01)
  - `GESTURE_TAP_AND_HOLD` (0x02)
  - Swipe gestures: X_NEG (0x04), X_POS (0x08), Y_POS (0x10), Y_NEG (0x20)

- **gestures1 (Multi-finger)**:
  - `GESTURE_TWO_FINGER_TAP` (0x01)
  - `GESTURE_SCROLL` (0x02)
  - `GESTURE_ZOOM` (0x04)

### Raw Data Structure
```c
struct iqs5xx_rawdata {
    uint8_t gestures0;        // Single finger gestures
    uint8_t gestures1;        // Multi-finger gestures
    uint8_t system_info0;     // System status
    uint8_t system_info1;     // Additional status
    uint8_t finger_count;     // Number of active fingers
    int16_t rx, ry;          // Relative movement
    struct iqs5xx_finger fingers[5]; // Individual finger data
};
```

## State Management

### Global State (`struct gesture_state`)
```c
struct gesture_state {
    // Movement accumulation for sub-pixel precision
    struct { float x, y; } accumPos;
    
    // Single finger drag state
    bool isDragging;
    bool dragStartSent;
    
    // Two finger state
    bool twoFingerActive;
    int64_t twoFingerStartTime;
    struct { uint16_t x, y; } twoFingerStartPos[2];
    
    // Three finger state
    bool threeFingersPressed;
    int64_t threeFingerPressTime;
    
    // Configuration
    uint8_t mouseSensitivity;
    uint8_t lastFingerCount;
};
```

## Event Flow

### 1. Hardware Interrupt
1. IQS5XX asserts RDY pin
2. GPIO interrupt triggers work queue item
3. I2C read of gesture and position data
4. `trackpad_trigger_handler()` called with raw data

### 2. Gesture Processing
1. **Immediate hardware gesture handling** - Process hardware-detected gestures first
2. **Finger count state management** - Handle finger lift/add events
3. **Movement processing** - Handle cursor/scroll movement
4. **State cleanup** - Reset states when appropriate

### 3. Input Event Generation
1. Mouse events: `INPUT_EV_KEY` (buttons), `INPUT_EV_REL` (movement)
2. Keyboard events: Via ZMK's HID system
3. Event synchronization and rate limiting

## Configuration

### Device Tree Configuration
```dts
&trackpad {
    compatible = "azoteq,iqs5xx";
    reg = <0x74>;
    dr-gpios = <&gpio0 2 GPIO_ACTIVE_LOW>;
    sensitivity = <128>;        // Mouse sensitivity (64-255)
    invert-x;                  // Coordinate transformations
    rotate-90;
};
```

### Build Configuration
- Kconfig options in `Kconfig`
- CMake integration in `CMakeLists.txt`
- Zephyr module definition in `zephyr/module.yml`

## Key Design Decisions

### 1. Modular Architecture
- Separate files for each gesture type enable easier maintenance
- Shared state structure provides coordination
- Clear separation of hardware interface and gesture logic

### 2. Hardware-First Approach
- Process hardware-detected gestures immediately for responsiveness
- Use hardware gesture events as primary triggers
- Fall back to position-based detection for complex gestures

### 3. State Management
- Centralized state in `gesture_state` structure
- Clear state transitions and cleanup
- Conflict resolution between competing gestures

### 4. Performance Optimizations
- Event rate limiting to prevent system overload
- Idle detection and reduced processing
- Sub-pixel movement accumulation
- Efficient I2C communication with error handling

## Future Extensions

### Planned Features
- **Advanced Drag and Drop**: Mac-style drag lock with second finger continuation
- **Custom Gesture Recognition**: User-defined gesture patterns
- **Adaptive Sensitivity**: Context-aware sensitivity adjustment
- **Multi-device Support**: Support for multiple trackpad configurations

### Extension Points
- Additional gesture handlers can be added to `src/` and registered in `trackpad.c`
- New keyboard shortcuts can be added to `trackpad_keyboard_events.c`
- Hardware configurations can be extended via device tree bindings