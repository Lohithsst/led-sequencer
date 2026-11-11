// Debounce time
#define debounce 200

// Speed control constants for potentiometer scaling
#define speedDiv 20      // Divides pot value to get reasonable range
#define speedMin 10      // Minimum speed so animation never stops
#define speedMult 10     // Multiplier for final delay time
#define patternCount 5   // Total number of patterns (0 to 4)

// All variables are volatile because they're used in interrupts
volatile unsigned char ledPos = 0;       // Current LED position (0-7)
volatile unsigned char patSelect = 0;    // Track pattern
volatile unsigned char pwmCount = 0;           // Counter for software PWM
volatile unsigned char bright = 128;           // Brightness level (0-255), starts at 50% but overwritten by pot reading after its read
volatile unsigned char mode = 0;               // 0 = CTC (software PWM), 1 = hardware PWM

volatile unsigned char bounceDir = 0;          // Bounce direction: 0 = right, 1 = left
volatile unsigned char fillCount = 1;          // bounceFill led count

volatile unsigned long lastBtn0 = 0;     // Last time button 0 was pressed
volatile unsigned long lastBtn1 = 0;     // Last time button 1 was pressed

// Individual brightness for each LED
volatile unsigned char ledBright[8] = {0, 0, 0, 0, 0, 0, 0, 0};

void adc_init(void) {
    ADMUX = 0x40;
    ADCSRA = 0x87;
}

unsigned int adc_read(unsigned char adcx) {
    ADMUX &= 0xF0; 
    ADMUX |= adcx;
    ADCSRA |= 0x40; 
    while ((ADCSRA & 0x40));
    return ADCL | (ADCH << 8);
}

void initPWM(void) {
    TIMSK2 = 0x00;
    TCCR2A = 0x83;
    TCCR2B = 0x04;
    DDRB |= 0x08;
    OCR2A = bright;
    mode = 1; 
}

void initCTC(void) {
    TCCR2A = 0x02;
    TCCR2B = 0x03;
    OCR2A = 0x40;
    TIMSK2 = 0x02;
    pwmCount = 0;
    mode = 0;
}

// Turns off all LEDs
void clearLeds(void) {
    PORTD &= 0x0F;         // Clear PD4-PD7
    PORTB &= 0x08;         // Clear PB0-2 and PB4 (LEDs 4-7), keep PB3
}

// Button interrupts
void interrupt_init(void) {
    EICRA = 0x0A; // falling edge
    EIMSK = 0x03;
}

// Button 1 (pattern select)
ISR(INT0_vect) {
    if (millis() - lastBtn0 > debounce) {
        // pattern selection
        patSelect++;
        if (patSelect >= patternCount){
            patSelect = 0;
        }

        // reset everything
        bounceDir = 0;
        ledPos = 0;
        fillCount = 1;

        for (unsigned char i = 0; i < 8; i++){
            ledBright[i] = 0;
        }
        clearLeds();


        lastBtn0 = millis(); // Remember last press time
    }
}

// Button 2 (mode select)
ISR(INT1_vect) {
    if (millis() - lastBtn1 > debounce) {
        if (mode == 0){
            initPWM();
        }
        else {
            initCTC();
        }
        lastBtn1 = millis();
    }
}

// Set LEDs
void setLeds(unsigned char mask) {
    // LEDs 0-3 are on PD4-PD7, so shift mask left by 4
    PORTD = (PORTD & 0x0F) | (mask << 4);
    
    // LEDs 4-6 are on PB0-2, LED 7 is on PB4
    // (mask >> 4) & 0x07 gets bits 4-6 into position 0-2
    // (mask & 0x80) >> 3 moves bit 7 to position 4
    PORTB = (PORTB & 0x08) | ((mask >> 4) & 0x07) | ((mask & 0x80) >> 3);
}

// (16*10^6)/(32*64) = 7812.5 interrupts per second
// 7812.5/255 = 30hz (terrible flicker)
// 7812.4/32 = 244hz (better than my monitor refresh)
ISR(TIMER2_COMPA_vect) {
    pwmCount++;
    
    // Only patterns 0 and 1 use the "comet" effect
    if (patSelect >= 2) {
        return;
    }

    PORTD &= 0x0F;
    PORTB &= 0x08;

    unsigned char pwmCycle = pwmCount & 0x1F; // gives last 5 bits (0b0001 1111) --> resets after 31 so 32 steps
    
    // For each LED: if cycle count < brightness, turn LED on
    // ledBright stores brightness in 0-255, if we shift by 3 we can get the 0-31 scale
    if (pwmCycle < (ledBright[0] >> 3)) PORTD |= 0x10;
    if (pwmCycle < (ledBright[1] >> 3)) PORTD |= 0x20;
    if (pwmCycle < (ledBright[2] >> 3)) PORTD |= 0x40;
    if (pwmCycle < (ledBright[3] >> 3)) PORTD |= 0x80;
    if (pwmCycle < (ledBright[4] >> 3)) PORTB |= 0x01;
    if (pwmCycle < (ledBright[5] >> 3)) PORTB |= 0x02;
    if (pwmCycle < (ledBright[6] >> 3)) PORTB |= 0x04;
    if (pwmCycle < (ledBright[7] >> 3)) PORTB |= 0x10;
}

// Shows a single LED at position ledPos (for PWM mode)
void showLed(void) {
    clearLeds();
    
    // LED 0-3 → PORTD pins 4-7
    if (ledPos < 4) {
        PORTD |= (1 << (ledPos + 4));
    }
    // LED 4-6 → PORTB pins 0-2
    else if (ledPos < 7) {
        PORTB |= (1 << (ledPos - 4));
    }
    // LED 7 → PORTB pin 4
    else {
        PORTB |= (1 << 4);
    }
}

// Comet tail effect
void setCometBrightness(unsigned char headPos, char direction) { // +1 for left, -1 for right

    for (unsigned char i = 0; i < 8; i++){
        ledBright[i] = 0;
    }

    // "Head" LED at full brightness
    ledBright[headPos] = bright;
    
    // First tail LED
    char tailPos = headPos - direction;
    if (tailPos >= 0 && tailPos <= 7) {
        ledBright[tailPos] = bright / 3;
    }
    // Second tail LED
    tailPos = headPos - (direction * 2);
    if (tailPos >= 0 && tailPos <= 7) {
        ledBright[tailPos] = bright / 5;
    }
}

void setup() {
    DDRD = 0xF0; // PD4-7 outputs, PD2-3 inputs
    PORTD = 0x0C; // Enable pull-ups on PD2-3 
    DDRB = 0x1F; // PB0-4 as outputs
    PORTB = 0x00;
    
    adc_init();
    interrupt_init();
    
    mode = 0;
    initCTC();
}

// Pattern 0: Left-Right rotation
void rotateLeft(void) {
    unsigned int pot = adc_read(0);
    bright = adc_read(1) / 4; // Brightness (0-1023 from potentiometer -> 0-255)
    
    if (mode == 1) {
        OCR2A = bright; // update brightness for PWM mode
        showLed();
    } else {
        setCometBrightness(ledPos, 1);
    }
    
    // Higher pot value = faster (shorter delay)
    int spd = ((1023 - pot) / speedDiv) + speedMin;
    delay(spd * speedMult);
    
    ledPos++;
    if (ledPos > 7){
        ledPos = 0;
    }
}

// Pattern 1: Right-Left rotation
void rotateRight(void) {
    unsigned int pot = adc_read(0);
    bright = adc_read(1) / 4;
    
    if (mode == 1) {
        OCR2A = bright;
        showLed();
    } else {
        setCometBrightness(ledPos, -1);
    }
    
    int spd = ((1023 - pot) / speedDiv) + speedMin;
    delay(spd * speedMult);

    if (ledPos == 0){
        ledPos = 7;
    }
    else {
        ledPos--;
    }
}

// Pattern 2: Bounce fill
void bounceFill(void) {
    unsigned int pot = adc_read(0);
    bright = adc_read(1) / 4;
    
    if (mode == 1){
        OCR2A = bright;
    }

    // Turn on fillCount LEDs in a row, starting at position ledPos
    unsigned char mask = 0;
    for (unsigned char i = 0; i < fillCount; i++) {
        mask |= (1 << (ledPos + i));
    }
    setLeds(mask);
    
    int spd = ((1023 - pot) / speedDiv) + speedMin;
    delay(spd * speedMult);
    
    if (bounceDir == 0) {  // Moving right
        // Check if we hit the right edge
        if (ledPos + fillCount >= 8) {
            bounceDir = 1; // Reverse direction
            fillCount++;   // Add one more LED
            
            // If all 8 LEDs filled, celebrate and reset
            if (fillCount >= 8) {
                for (int i = 0; i < 5; i++) {
                    setLeds(0xFF);
                    delay(200);
                    setLeds(0x00);
                    delay(200);
                }
                fillCount = 1;
                ledPos = 0;
                bounceDir = 0;
            } else {
                ledPos = 8 - fillCount;  // Position group at right edge
            }
        } else {
            ledPos++;
        }
    } else {  // Moving left
        // Check if we hit the left edge
        if (ledPos == 0) {
            bounceDir = 0; // Reverse direction
            fillCount++;   // Add one more LED
            
            if (fillCount >= 8) {
                for (int i = 0; i < 5; i++) {
                    setLeds(0xFF);
                    delay(200);
                    setLeds(0x00);
                    delay(200);
                }
                fillCount = 1;
                ledPos = 0;
                bounceDir = 0;
            }
            // ledPos already 0, no adjustment needed
        } else {
            ledPos--;
        }
    }
}

// Pattern 3: Alternate
void alternate(void) {
    unsigned int pot = adc_read(0);
    bright = adc_read(1) / 4;  // Read brightness pot
    
    if (mode == 1) {
        OCR2A = bright;  // Update brightness for PWM mode
    }
    
    // 0x55 = 01010101 (even LEDs), 0xAA = 10101010 (odd LEDs)
    if (ledPos == 0) {
        setLeds(0x55);
    }
    else {
        setLeds(0xAA);
    }
    
    int spd = ((1023 - pot) / speedDiv) + speedMin;
    delay(spd * speedMult * 2);    // Slower for this pattern
    
    // Toggle between 0 and 1
    if (ledPos == 0) {
        ledPos = 1;
    }
    else {
        ledPos = 0;
    }
}

// Pattern 4: Tug of war
void tugOfWar(void) {
    EIMSK = 0x00; // Disable interrupts since we poll buttons in this game
    char pos = 3; // Start in middle
    unsigned char prevBtn1 = 1, prevBtn2 = 1; // Previous button states (1 = not pressed)
    
    setLeds(0x00);
    setLeds(1 << pos); // Show starting position
    delay(500); // Give players time to get ready
    
    // Game loop - continues until LED reaches position 0 or 7
    while (pos > 0 && pos < 7) {
        unsigned char btn1 = (PIND & 0x04); // Button 1
        unsigned char btn2 = (PIND & 0x08); // Button 2
        
        // Only move on new press (not while held)
        if (btn1 == 0 && prevBtn1 != 0) {
            pos--; // Button 1 moves left
        }
        if (btn2 == 0 && prevBtn2 != 0) { 
            pos++; // Button 2 moves right
        }
        prevBtn1 = btn1;
        prevBtn2 = btn2;
        
        setLeds(1 << pos);
        delay(20);
    }
    
    // Flash the winning side
    // 0x07 = 00000111 (left 3 LEDs), 0xE0 = 11100000 (right 3 LEDs)
    unsigned char winMask;
    if (pos == 0) {
        winMask = 0x07;
    }
    else {
        winMask = 0xE0;
    }
    for (int i = 0; i < 5; i++) {
        setLeds(winMask);
        delay(200);
        setLeds(0x00);
        delay(200);
    }
    
    // Reset debounce timers so button presses during game are ignored
    lastBtn0 = millis();
    lastBtn1 = millis();

    EIMSK = 0x03; // Re-enable button interrupts
    
    // Reset to pattern 0
    patSelect = 0;
    ledPos = 0;
    fillCount = 1;
    bounceDir = 0;
    
    for (unsigned char i = 0; i < 8; i++){
        ledBright[i] = 0;
    }
    initCTC(); // Restart CTC mode for comet effect
}

void loop(void) {
    switch (patSelect) {
        case 0: rotateLeft();  break;
        case 1: rotateRight(); break;
        case 2: bounceFill();  break;
        case 3: alternate();   break;
        case 4: tugOfWar();    break;
    }
}