# Mac-Style Drag and Drop Implementation

## Overview
This document describes the implementation of Mac-style drag and drop functionality in the ZMK Azoteq IQS5XX driver. The feature allows users to:

1. **Press and hold** with one finger to initiate drag lock
2. **Continue dragging** with a second finger while keeping the first finger pressed
3. **Release the first finger** to end the drag operation

This mimics the behavior of macOS trackpads and provides a more comfortable dragging experience for large movements.

## How It Works

### User Experience
1. **Initiate Drag Lock**: 
   - Tap and hold one finger on the trackpad for 300ms without moving it significantly
   - The system enters "drag lock" mode and sends a mouse button press event
   - The first finger is now "locked" in drag mode

2. **Continue Dragging**:
   - While keeping the first finger pressed, use a second finger to move the cursor
   - The first finger stays in place (locked) while the second finger provides movement
   - All cursor movement comes from the second finger

3. **End Drag**:
   - Lift the first finger to release the mouse button and exit drag lock mode
   - The dragged item is dropped at the current cursor position

### Technical Implementation

#### State Management
New state variables added to `struct gesture_state`:

```c
// Mac-style drag lock state
bool dragLockActive;           // True when first finger is "locked" in drag mode
bool dragLockButtonSent;       // True when button press was sent for drag lock
int64_t dragLockStartTime;     // When drag lock was initiated
uint16_t dragLockStartX;       // Initial position when drag lock started
uint16_t dragLockStartY;
bool secondFingerMoving;       // True when second finger is providing movement
uint8_t dragLockFingerID;      // ID of the finger that initiated drag lock

// Movement state for second finger during drag lock
struct {
    float x;
    float y;
} dragLockAccumPos;
```

#### Configuration Constants
```c
#define DRAG_LOCK_HOLD_TIME_MS             300    // Time to hold finger before drag lock activates
#define DRAG_LOCK_MAX_MOVEMENT_PX          50     // Maximum movement allowed during hold time
#define DRAG_LOCK_MOVEMENT_THRESHOLD       0.5f   // Movement threshold for second finger during drag lock
```

### Implementation Details

#### 1. Drag Lock Initiation (`src/single_finger.c`)
When a `GESTURE_TAP_AND_HOLD` hardware gesture is detected:

1. **Initialize drag lock state**:
   - Set `dragLockActive = true`
   - Record start time and position
   - Set up monitoring for the hold period

2. **Monitor hold period**:
   - Check if finger moves more than `DRAG_LOCK_MAX_MOVEMENT_PX` (cancels drag lock)
   - After `DRAG_LOCK_HOLD_TIME_MS` elapses, send mouse button press
   - Set `dragLockButtonSent = true`

#### 2. Movement Handling
**Single Finger Mode** (`handle_single_finger_gestures()`):
- If drag lock is active and button was sent, ignore movement from first finger
- First finger is "locked" in place

**Two Finger Mode** (`handle_drag_lock_gestures()`):
- Only process if drag lock is active with button sent
- Use relative movement data (`data->rx`, `data->ry`) for second finger movement
- Apply sensitivity scaling and movement thresholds
- Send cursor movement events

#### 3. State Transitions
**Normal Operation → Drag Lock**:
- Triggered by `GESTURE_TAP_AND_HOLD`
- Transitions through monitoring phase to active drag lock

**Drag Lock → Two Finger Dragging**:
- Automatically when second finger is detected
- First finger remains locked, second finger provides movement

**End Drag Lock**:
- When first finger is lifted (finger count goes to 0 or 1)
- Sends mouse button release event
- Clears all drag lock state

### Integration Points

#### Main Controller (`src/trackpad.c`)
Modified finger count handling to support drag lock:

```c
case 2:
    // Check if this is drag lock mode (first finger locked, second finger for movement)
    if (g_gesture_state.dragLockActive && g_gesture_state.dragLockButtonSent) {
        // Handle drag lock with two fingers - don't reset single finger state
        handle_drag_lock_gestures(dev, data, &g_gesture_state);
    } else {
        // Normal two finger gestures (scrolling, zoom, etc.)
        handle_two_finger_gestures(dev, data, &g_gesture_state);
    }
    break;
```

#### Function Hierarchy
- `handle_single_finger_gestures()` - Handles drag lock initiation and single finger movement
- `handle_drag_lock_gestures()` - Handles two-finger movement during drag lock
- `reset_single_finger_state()` - Cleans up both normal drag and drag lock states
- `reset_drag_lock_state()` - Specifically cleans up drag lock state

## Behavior Differences from Standard Dragging

### Standard Drag (`GESTURE_TAP_AND_HOLD`)
- Immediate mouse button press on tap-and-hold detection
- Same finger that initiated drag continues to provide movement
- Button released when finger is lifted

### Drag Lock Mode
- Delayed mouse button press (after 300ms hold time)
- First finger becomes "locked" and doesn't provide movement
- Second finger provides all movement while first finger stays pressed
- Button released when first finger is lifted

## Compatibility

### Coexistence with Other Gestures
- **Two-finger scrolling/zoom**: Disabled during drag lock mode
- **Three-finger gestures**: Will terminate drag lock (as designed)
- **Single tap**: Works normally when not in drag lock mode

### Fallback Behavior
- If finger moves too much during hold period, falls back to normal drag
- If second finger never appears, acts like normal single-finger drag
- Graceful cleanup on unexpected state transitions

## Debugging and Troubleshooting

### Common Issues
1. **Drag lock not activating**: Check hold time and movement thresholds
2. **Accidental drag lock**: User moved finger too much during hold - adjust `DRAG_LOCK_MAX_MOVEMENT_PX`
3. **Jerky movement**: Adjust `DRAG_LOCK_MOVEMENT_THRESHOLD` for smoother second finger tracking

### State Debugging
Key state variables to monitor:
- `dragLockActive`: Should be true during entire drag lock session
- `dragLockButtonSent`: Should be true after hold time elapses
- `secondFingerMoving`: Should be true when second finger provides movement
- `dragLockStartTime`: Timestamp of drag lock initiation

## Future Enhancements

### Possible Improvements
1. **Visual/Haptic Feedback**: Provide feedback when drag lock activates
2. **Configurable Timing**: Make hold time and thresholds user-configurable
3. **Multi-Selection**: Support for multiple items with modifier keys
4. **Gesture Conflicts**: Better resolution of conflicts with other gestures

### Performance Considerations
- Minimal CPU overhead: only processes when in appropriate finger count states
- Memory efficient: reuses existing state structure
- Event efficient: doesn't generate excessive input events

## Testing Scenarios

### Basic Functionality
1. Single tap → normal click
2. Tap and hold → drag lock activation after 300ms
3. Two finger movement during drag lock → cursor movement
4. Lift first finger → end drag operation

### Edge Cases
1. Rapid finger lift/placement during hold period
2. Three fingers during drag lock (should terminate)
3. Very small movements during hold period
4. Hardware gesture conflicts

### Performance Tests
1. Extended drag operations (large movements)
2. Rapid finger transitions
3. Multiple drag lock sessions in sequence