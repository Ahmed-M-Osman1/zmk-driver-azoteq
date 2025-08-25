# Testing Checklist for Mac-Style Drag and Drop

## Pre-Testing Setup
- [ ] Ensure trackpad is properly connected and configured
- [ ] Verify I2C communication is working
- [ ] Check that basic single-finger cursor movement works
- [ ] Confirm device tree configuration is correct

## Basic Functionality Tests

### Single Finger Operations
- [ ] **Cursor Movement**: Move one finger → cursor moves smoothly
- [ ] **Single Tap**: Quick tap → left mouse click
- [ ] **Standard Drag**: Tap and hold + move → standard drag operation
- [ ] **Movement Sensitivity**: Test different movement speeds and distances

### Mac-Style Drag Lock Tests

#### Drag Lock Activation
- [ ] **Hold Test**: Tap and hold one finger for 0.3+ seconds → drag lock activates
- [ ] **Movement Cancellation**: Move finger during hold period → falls back to normal drag
- [ ] **Light Touch**: Test with very light finger pressure
- [ ] **Heavy Touch**: Test with heavier finger pressure

#### Two-Finger Drag Operation
- [ ] **Second Finger Movement**: After drag lock, add second finger → cursor moves with second finger
- [ ] **First Finger Locked**: Verify first finger doesn't affect cursor during drag lock
- [ ] **Smooth Movement**: Second finger movement → smooth cursor tracking
- [ ] **Large Movements**: Test dragging across entire screen

#### Drag Lock Release
- [ ] **First Finger Release**: Lift first finger → mouse button released, drag ends
- [ ] **Second Finger First**: Lift second finger first, then first → proper cleanup
- [ ] **Both Fingers**: Lift both fingers simultaneously → proper cleanup

### Edge Case Tests

#### Timing Tests
- [ ] **Quick Release**: Hold for less than 0.3s then release → no drag lock
- [ ] **Hold Variations**: Test different hold times (0.2s, 0.5s, 1s)
- [ ] **Rapid Gestures**: Multiple quick tap-and-holds in sequence

#### Movement Tests
- [ ] **Tiny Movements**: Small movements during hold period
- [ ] **Large Movements**: Movements exceeding 50px during hold period → should cancel drag lock
- [ ] **Circular Movements**: Move in circles during hold period
- [ ] **Jittery Fingers**: Test with naturally unsteady fingers

#### Multi-Finger Scenarios
- [ ] **Three Fingers During Drag**: Add third finger during drag lock → should terminate drag
- [ ] **Four+ Fingers**: Add multiple fingers → proper state cleanup
- [ ] **Finger Lifting Order**: Various orders of finger lifting

### Interaction with Other Gestures

#### Two-Finger Gestures
- [ ] **Normal Two-Finger**: Without drag lock → scrolling/zoom works
- [ ] **Scroll Prevention**: During drag lock → scrolling disabled
- [ ] **Right Click**: Two-finger tap → should work normally when not in drag lock

#### Three-Finger Gestures
- [ ] **Mission Control**: Three-finger tap → should work and terminate any drag lock
- [ ] **App Switching**: Three-finger swipes → should work and terminate drag lock

#### Hardware Gesture Priority
- [ ] **Hardware vs Software**: Hardware gestures take priority over drag lock
- [ ] **Gesture Conflicts**: Test simultaneous gesture attempts

## Performance Tests

### Responsiveness
- [ ] **Gesture Recognition Speed**: Time from tap to drag lock activation
- [ ] **Movement Lag**: Delay between second finger movement and cursor response
- [ ] **Button Release Speed**: Time from finger lift to mouse button release

### Resource Usage
- [ ] **CPU Usage**: Monitor CPU during extended drag operations
- [ ] **Memory Leaks**: Check for memory leaks during repeated operations
- [ ] **Event Rate**: Verify appropriate event rate limiting

### Power Management
- [ ] **Idle Transition**: Drag lock during idle periods
- [ ] **Wake from Sleep**: Drag lock after trackpad wake-up
- [ ] **Power Efficiency**: Extended use doesn't cause excessive power drain

## Real-World Usage Tests

### File Management
- [ ] **File Dragging**: Drag files between folders
- [ ] **Desktop Icons**: Move desktop icons around
- [ ] **Multiple Items**: Select and drag multiple items

### Text Editing
- [ ] **Text Selection**: Select text with drag lock
- [ ] **Text Movement**: Drag and drop text blocks
- [ ] **Precision Editing**: Fine positioning of cursor/selections

### Web Browsing
- [ ] **Link Dragging**: Drag links to new tabs/windows
- [ ] **Image Dragging**: Save images by dragging
- [ ] **Tab Management**: Drag tabs between browser windows

### Application Testing
- [ ] **IDE Usage**: Code selection and movement
- [ ] **Image Editors**: Object manipulation
- [ ] **CAD Applications**: Precise object positioning
- [ ] **Gaming**: If applicable to your use case

## Error Conditions

### Hardware Issues
- [ ] **Connection Loss**: I2C communication failure during drag
- [ ] **Noisy Signal**: Electrical interference during operation
- [ ] **Multiple Trackpads**: If multiple devices are connected

### Software Issues
- [ ] **State Corruption**: Unexpected state transitions
- [ ] **Memory Issues**: Out of memory conditions
- [ ] **Race Conditions**: Rapid state changes

### Recovery Testing
- [ ] **Graceful Degradation**: Fallback when features fail
- [ ] **State Recovery**: Recovery from corrupted states
- [ ] **Error Reporting**: Appropriate error messages/logs

## Documentation Verification

### User Experience
- [ ] **Usage Guide Accuracy**: Instructions match actual behavior
- [ ] **Timing Values**: Documented timings match implementation
- [ ] **Configuration Options**: Device tree settings work as documented

### Developer Documentation
- [ ] **Architecture Document**: Technical details are accurate
- [ ] **State Diagrams**: State transitions match implementation
- [ ] **Configuration Examples**: Sample configurations work

## Regression Tests

### Existing Functionality
- [ ] **Single-Finger Gestures**: No regression in basic functionality
- [ ] **Two-Finger Gestures**: Scrolling and zoom still work
- [ ] **Three-Finger Gestures**: System shortcuts still work
- [ ] **Hardware Gestures**: All hardware-detected gestures still work

### Performance Regression
- [ ] **Cursor Responsiveness**: No degradation in cursor tracking
- [ ] **Gesture Recognition**: No delays in gesture detection
- [ ] **Power Consumption**: No significant increase in power usage

## Test Results Template

For each test, record:
- **Status**: ✅ Pass / ❌ Fail / ⚠️ Partial
- **Notes**: Specific observations
- **Issues**: Any problems encountered
- **Recommendations**: Suggested improvements

## Example Test Log Entry
```
Test: Drag Lock Activation
Date: [Date]
Status: ✅ Pass
Notes: Drag lock activates consistently after 0.3s hold
Issues: None
Recommendations: Consider haptic feedback when available
```