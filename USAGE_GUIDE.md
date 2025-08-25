# ZMK Azoteq IQS5XX Trackpad Usage Guide

## Overview
This guide explains how to use all the gesture features available in the ZMK Azoteq IQS5XX trackpad driver, including the new Mac-style drag and drop functionality.

## Supported Gestures

### Single Finger Gestures

#### Basic Cursor Movement
- **Action**: Move one finger on the trackpad
- **Result**: Moves the cursor/mouse pointer
- **Notes**: Sensitivity can be configured via device tree

#### Single Tap (Left Click)
- **Action**: Tap once with one finger
- **Result**: Left mouse click
- **Timing**: Quick tap and lift

#### Standard Drag
- **Action**: Tap and hold one finger, then move it
- **Result**: Left mouse button press + drag + release when lifted
- **Use Case**: Standard drag and drop, selecting text

### Mac-Style Drag and Drop (NEW!)

#### How to Use Mac-Style Drag Lock:
1. **Start Drag Lock**: 
   - Tap and hold one finger firmly on the trackpad
   - Keep it still for about 0.3 seconds
   - You'll feel the drag lock activate (mouse button pressed)

2. **Continue Dragging**: 
   - While keeping the first finger pressed down, use a second finger to move the cursor
   - The first finger stays locked in place
   - Move the second finger to drag the item to where you want

3. **Drop**: 
   - Lift the first finger to release the mouse button and drop the item
   - The second finger can be lifted before or after

#### When to Use Mac-Style Drag Lock:
- **Long distance drags**: Moving files across large screens
- **Precision placement**: When you need to carefully position items
- **Comfort**: Reduces finger fatigue during extended drag operations
- **Multi-monitor setups**: Easier to drag items between monitors

### Two Finger Gestures

#### Right Click
- **Action**: Tap with two fingers simultaneously
- **Result**: Right mouse click (context menu)
- **Notes**: Hardware-detected gesture for instant response

#### Two-Finger Scrolling
- **Vertical Scroll**: Move two fingers up/down together
- **Horizontal Scroll**: Move two fingers left/right together
- **Notes**: Automatic gesture detection based on movement pattern

#### Pinch to Zoom
- **Action**: Move two fingers apart (zoom in) or together (zoom out)
- **Result**: Sends zoom in/out commands to the system
- **Use Case**: Web browsers, image viewers, maps

### Three Finger Gestures

#### Mission Control
- **Action**: Tap with three fingers
- **Result**: Cmd+F3 (opens Mission Control on macOS)
- **Timing**: Quick tap with all three fingers

#### App Switching
- **Three-finger swipe left**: Cmd+Tab (next application)
- **Three-finger swipe right**: Cmd+Shift+Tab (previous application)
- **Three-finger swipe up**: Cmd+F3 (Mission Control)

## Configuration

### Device Tree Settings
Add these properties to your trackpad configuration:

```dts
&trackpad {
    compatible = "azoteq,iqs5xx";
    reg = <0x74>;
    dr-gpios = <&gpio0 2 GPIO_ACTIVE_LOW>;
    
    // Sensitivity: 64-255 (higher = more sensitive)
    sensitivity = <128>;
    
    // Coordinate transformations (if needed)
    invert-x;           // Flip X axis
    invert-y;           // Flip Y axis
    rotate-90;          // Rotate 90 degrees
    rotate-180;         // Rotate 180 degrees
    rotate-270;         // Rotate 270 degrees
};
```

### Sensitivity Adjustment
- **Low sensitivity (64-100)**: More precise cursor movement, requires more finger movement
- **Medium sensitivity (100-150)**: Balanced for general use
- **High sensitivity (150-255)**: Fast cursor movement, requires less finger movement

## Tips and Best Practices

### For Mac-Style Drag Lock:
1. **Hold Still**: Keep the first finger very still during the 0.3 second hold period
2. **Use Light Touch**: Don't press too hard with the locked finger
3. **Second Finger Movement**: Use smooth, continuous movements with the second finger
4. **Practice**: The gesture becomes natural with a little practice

### For General Use:
1. **Clean Trackpad**: Keep the trackpad surface clean for best tracking
2. **Light Touch**: Use light finger pressure, the trackpad is very sensitive
3. **Finger Placement**: Use fingertips rather than finger pads for best precision
4. **Movement Speed**: Slower movements provide more precision

### Troubleshooting Common Issues:

#### Drag Lock Not Activating
- **Cause**: Finger moved too much during hold period
- **Solution**: Hold finger more still, reduce pressure

#### Accidental Drag Lock
- **Cause**: Unintentional tap-and-hold gesture
- **Solution**: Use quicker taps for single clicks

#### Jerky Cursor Movement
- **Cause**: Sensitivity too high or finger pressure inconsistent
- **Solution**: Lower sensitivity setting, use lighter touch

#### Gestures Not Working
- **Cause**: Finger strength too low, dirty trackpad
- **Solution**: Clean trackpad surface, ensure firm finger contact

## Advanced Usage

### Combining Gestures
- You can use different gestures in sequence
- Gesture priority: Three-finger > Two-finger > Single-finger
- Drag lock has priority over two-finger gestures when active

### Power Management
- The trackpad automatically enters low-power mode after 5 seconds of inactivity
- Any touch or gesture will wake it up instantly
- No user intervention needed for power management

### Multi-Application Support
The trackpad works with:
- **Text Editors**: Select text, drag and drop text blocks
- **File Managers**: Drag files and folders
- **Web Browsers**: Scroll pages, zoom content, select text
- **Image Editors**: Pan and zoom images
- **IDEs**: Navigate code, select and move code blocks

## Keyboard Integration

The trackpad can send keyboard shortcuts for system-level operations:
- **Three-finger gestures**: Send Cmd+ combinations
- **Zoom gestures**: Can be configured to send keyboard zoom shortcuts
- **Custom shortcuts**: Additional shortcuts can be programmed

## Hardware Specifications

### IQS5XX Chip Features:
- **Resolution**: Up to 1280x800 tracking points
- **Update Rate**: Up to 180Hz
- **Finger Detection**: Up to 5 simultaneous fingers
- **Gesture Engine**: Hardware-accelerated gesture recognition
- **Communication**: I2C interface
- **Power**: Low power consumption with automatic sleep

### Supported Hardware:
- Any keyboard or device with an IQS5XX trackpad controller
- I2C GPIO connection for data ready signal
- 3.3V power supply

## Getting Help

If you experience issues:

1. **Check Configuration**: Verify device tree settings
2. **Test Basic Gestures**: Start with single finger movement
3. **Clean Hardware**: Ensure trackpad surface is clean
4. **Check Logs**: Look for error messages in system logs
5. **Report Issues**: Use the project's GitHub issues page

## Version History

- **v1.0**: Basic single and multi-finger gestures
- **v2.0**: Added Mac-style drag lock functionality
- **Current**: Enhanced gesture recognition and power management