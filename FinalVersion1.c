#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/* Cyclone V FPGA devices */
/* DELETE WHEN COPYING ARM ADDRESSES.H*/
#define LEDR_BASE             0xFF200000
#define PS2_BASE              0xFF200100
#define A9_TIMER              0xFFFEC600
#define HEX3_HEX0_BASE        0xFF200020

//Globals
int counter = 0;
int numOfLanes = 15;
int numOfColumns = 20;
int Board[15][20];
int box_width = 16;
int box_height = 16;
struct Lane{
    int type;
    int speed;
    int density;
};
struct Lane Lanes[15];
int frog[2];
enum typesOfLanes{ safeLane, leftCarLane, rightCarLane, leftBusLane, rightBusLane, riverLane };
enum speedOfLanes{ infSpeed, veryFast, fast, medium, slow, verySlow, notMoving};
enum densityOfLanes { infDensity, veryHigh, high, moderate, low, veryLow, zero };
enum boardElements { safeSpace, leftCar, rightCar, leftBus, rightBus, water, woodenLog};

volatile int pixel_buffer_start; // global variable

//functions
void startLevel(int i);
void runLevel();
void initLevel1();
void initLevel2();
void initLevel3();
void initLevel4();
void initLevel5();
void initLevel6();
void initLevel7();
void initLevel8();
void initLevel9();
void initLevel10();
void youWin();
void initializeBoard();
void initializeFrog();
void doDelay();
void pollTimer();
void updateFrog(); //not completed
bool isFrogDead();
void riverMoveFrog(int i);
bool didFrogWin();
void updateBoard();
void updateLeftCarLane(int i);
void updateRightCarLane(int i);
void updateLeftBusLane(int i);
void updateRightBusLane(int i);
void updateRiverLane(int i);
void clear_screen();
void printBoard(); //only terminal for debugging
void plot_pixel(int x, int y, short int line_color);
void plot_box(int x, int y, int type);
void wait_for_vsync();
void drawRoad(int x, int y);
void drawLeftCar(int x, int y);
void drawRightCar(int x, int y);
void drawFrog(int x, int y);
void drawLog(int x, int y);
void drawWater(int x, int y);
void drawBus(int x, int y);

// Input Globals
volatile int * PS2_ptr = (int *)PS2_BASE;
volatile int * LED_ptr = (int *)LEDR_BASE;
volatile int * A9_TIMER_ptr = (int *) A9_TIMER;
int PS2_data, RVALID;

// INTERRUPT CODE TO BE MIGRATED TO A SEPARATE FILE
void set_A9_IRQ_stack(void);
void config_GIC(void);
void config_HPS_timer(void);
void config_HPS_GPIO1(void);
void config_interval_timer(void);
void config_PS2(void);
void enable_A9_interrupts(void);
/* key_dir and pattern are written by interrupt service routines; we have to
* declare these as volatile to avoid the compiler caching their values in
* registers */
volatile int tick = 0; // set to 1 every time the HPS timer expires
volatile int key_dir = 0;
volatile int pattern = 0x0F0F0F0F; // pattern for LED lights

void disable_A9_interrupts (void);
void set_A9_IRQ_stack (void);
void config_GIC (void);
void enable_A9_interrupts (void);

// Recording PS2 Data
char byte1 = 0, byte2 = 0, byte3 = 0;

void config_PS2(void){
	volatile int * PS2_ptr = (int *) PS2_BASE;
	*(PS2_ptr) = 0xFF; // reset
	*(PS2_ptr + 1) = 0x1; // set the RE bit to 1
}
/* This file:
* 1. defines exception vectors for the A9 processor
* 2. provides code that sets the IRQ mode stack, and that dis/enables
* interrupts
* 3. provides code that initializes the generic interrupt controller
*/
void pushbutton_ISR (void);
void ps2_ISR(void);
void config_interrupt (int, int);

// Define the IRQ exception handler
void __attribute__ ((interrupt)) __cs3_isr_irq (void) {
	// Read the ICCIAR from the CPU Interface in the GIC
	int interrupt_ID = *((int *) 0xFFFEC10C);
	if (interrupt_ID == 79)
		ps2_ISR();
	else
		while (1); // if unexpected, then stay here
	// Write to the End of Interrupt Register (ICCEOIR)
	*((int *) 0xFFFEC110) = interrupt_ID;
}
// Define the remaining exception handlers
void __attribute__ ((interrupt)) __cs3_reset (void){ while(1); }
void __attribute__ ((interrupt)) __cs3_isr_undef (void){ while(1); }
void __attribute__ ((interrupt)) __cs3_isr_swi (void){ while(1); }
void __attribute__ ((interrupt)) __cs3_isr_pabort (void){ while(1); }
void __attribute__ ((interrupt)) __cs3_isr_dabort (void){ while(1); }
void __attribute__ ((interrupt)) __cs3_isr_fiq (void){ while(1); }
/*
* Turn off interrupts in the ARM processor
*/
void disable_A9_interrupts(void){
	int status = 0b11010011;
	asm("msr cpsr, %[ps]" : : [ps]"r"(status));
}
/*
* Initialize the banked stack pointer register for IRQ mode
*/
void set_A9_IRQ_stack(void){
	int stack, mode;
	stack = 0xFFFFFFFF - 7; // top of A9 onchip memory, aligned to 8 bytes
	/* change processor to IRQ mode with interrupts disabled */
	mode = 0b11010010;
	asm("msr cpsr, %[ps]" : : [ps] "r" (mode));
	/* set banked stack pointer */
	asm("mov sp, %[ps]" : : [ps] "r" (stack));
	/* go back to SVC mode before executing subroutine return! */
	mode = 0b11010011;
	asm("msr cpsr, %[ps]" : : [ps] "r" (mode));
}
/*
* Turn on interrupts in the ARM processor
*/
void enable_A9_interrupts(void){
	int status = 0b01010011;
	asm("msr cpsr, %[ps]" : : [ps]"r"(status));
}
/*
* Configure the Generic Interrupt Controller (GIC)
*/
void config_GIC(void){
	config_interrupt(79, 1);
	// Set Interrupt Priority Mask Register (ICCPMR). Enable all priorities
	*((int *) 0xFFFEC104) = 0xFFFF;
	// Set the enable in the CPU Interface Control Register (ICCICR)
	*((int *) 0xFFFEC100) = 1;
	// Set the enable in the Distributor Control Register (ICDDCR)
	*((int *) 0xFFFED000) = 1;
}
/*
* Configure registers in the GIC for an individual Interrupt ID. We
* configure only the Interrupt Set Enable Registers (ICDISERn) and
* Interrupt Processor Target Registers (ICDIPTRn). The default (reset)
* values are used for other registers in the GIC
*/
void config_interrupt (int N, int CPU_target){
	int reg_offset, index, value, address;
	/* Configure the Interrupt Set-Enable Registers (ICDISERn).
	* reg_offset = (integer_div(N / 32) * 4; value = 1 << (N mod 32) */
	reg_offset = (N >> 3) & 0xFFFFFFFC;
	index = N & 0x1F;
	value = 0x1 << index;
	address = 0xFFFED100 + reg_offset;
	/* Using the address and value, set the appropriate bit */
	*(int *)address |= value;
	/* Configure the Interrupt Processor Targets Register (ICDIPTRn)
	* reg_offset = integer_div(N / 4) * 4; index = N mod 4 */
	reg_offset = (N & 0xFFFFFFFC);
	index = N & 0x3;
	address = 0xFFFED800 + reg_offset + index;
	/* Using the address and value, write to (only) the appropriate byte */
	*(char *)address = (char) CPU_target;
}

/* PS2 - Interrupt Service Routine
* Records the value at PS2 and resets the interrupt bits
*/
void ps2_ISR(void){
	volatile int * PS2_ptr = (int *)PS2_BASE;
	volatile int * LED_ptr = (int *)LEDR_BASE;
	int PS2_data, RVALID;
	PS2_data = *(PS2_ptr); // read the Data register in the PS/2 port
	RVALID = PS2_data & 0x8000; // extract the RVALID field
	if (RVALID) {
		/* shift the next data byte into the display */
		byte1 = byte2;
		byte2 = byte3;
		byte3 = PS2_data & 0xFF;
		int RAVAIL = PS2_data & 0xFFFF0000;
		// clear the fifo 
		updateFrog();
		while(RAVAIL > 0){
			PS2_data = *(PS2_ptr);
			RAVAIL = PS2_data & 0xFFFF0000;
		}
	}
}

// boardElements with lengths
//leftcar -> Always length 1
//rightcar -> Always length 1
//leftbus -> Always length 2
//rightbus -> Always length 2
//woodenLog -> Always length 3


/* VGA colors */
#define WHITE 0xFFFF
#define YELLOW 0xFFE0
#define RED 0xF800
#define GREEN 0x07E0
#define BLUE 0x001F
#define CYAN 0x07FF
#define MAGENTA 0xF81F
#define GREY 0xC618
#define PINK 0xFC18
#define ORANGE 0xFC00
#define BLACK 0x0000
#define BROWN 0x79E0	
#define NAVY  0x000F
#define OLIVE 0x7BE0

int main(){ //just testing code functionality using terminal without timers, inputs or VGA displays
    volatile int * pixel_ctrl_ptr = (int *)0xFF203020;
    *(pixel_ctrl_ptr + 1) = 0xC8000000;
    pixel_buffer_start = *pixel_ctrl_ptr;
    /* set back pixel buffer to start of SDRAM memory */
    *(pixel_ctrl_ptr + 1) = 0xC0000000;
    pixel_buffer_start = *(pixel_ctrl_ptr + 1); // we draw on the back buffer
  	disable_A9_interrupts (); // disable interrupts in the A9 processor
  	set_A9_IRQ_stack (); // initialize the stack pointer for IRQ mode
  	config_GIC (); // configure the general interrupt controller
  	config_PS2(); // configure keyboard to gen interrupts
  	enable_A9_interrupts (); // enable interrupts in the A9 processor
	//Start of Game Logic
	startingPoint:
    startLevel(1);
    runLevel();
	if(isFrogDead()) goto startingPoint;
	startLevel(2);
    runLevel();
	if(isFrogDead()) goto startingPoint;
	startLevel(3);
    runLevel();
	if(isFrogDead()) goto startingPoint;
	startLevel(4);
    runLevel();
	if(isFrogDead()) goto startingPoint;
	startLevel(5);
    runLevel();
	if(isFrogDead()) goto startingPoint;
	startLevel(6);
    runLevel();
	if(isFrogDead()) goto startingPoint;
	startLevel(7);
    runLevel();
	if(isFrogDead()) goto startingPoint;
	startLevel(8);
    runLevel();
	if(isFrogDead()) goto startingPoint;
	startLevel(9);
    runLevel();
	if(isFrogDead()) goto startingPoint;
	startLevel(10);
    runLevel();
  if(isFrogDead()) goto startingPoint;
	youWin();
    return 0;
}

void initializeBoard(){
    for(int i = 0; i < numOfLanes; i++){
        if(Lanes[i].type != riverLane){
            for(int j = 0; j < numOfColumns; j++){
                Board[i][j] = 0;
            }
        }
        else{
            for(int j = 0; j < numOfColumns; j++){
                Board[i][j] = 5;
            }
        }
    }
}

void doDelay(){
  *(A9_TIMER_ptr) = 100 000;
  // for CPUlator use =500 000 for DE1 use =200 000 000 
  *(A9_TIMER_ptr + 2) = 3; // set enable bit to 1
  pollTimer();
  *(A9_TIMER_ptr + 3) = 1; // reset the f bit
  *(A9_TIMER_ptr + 2) = 2; // set enable bit to 0, stop clock
}

void pollTimer(){
  while (1){
    int a = *(A9_TIMER_ptr + 3);
    if (a == 1) return;
  }
}

void wait_for_vsync(){
	volatile int * pixel_ctrl_ptr = (int *)0xFF203020;
	int status;
	*pixel_ctrl_ptr=1;
	status=*(pixel_ctrl_ptr+3);
	while((status & 0x01)!=0){
		status=*(pixel_ctrl_ptr+3);
	}
	pixel_buffer_start = *(pixel_ctrl_ptr + 1);
}

void initializeFrog(){
    frog[0] = 0 ;
    frog[1] = numOfColumns/2;
}

void updateFrog(){
	int data = byte1<<16 | byte2<<8 | byte3;
  	switch(data & 0xFFFFFF){
    	case 0x00E0F075:
    	*(LED_ptr) = 0b1; // Up arrow
      	if (frog[0] < numOfLanes -1)
        	frog[0]+=1;
      	break;
    	case 0x00E0F072:
      	*(LED_ptr) = 0b10; // Down arrow
      	if (frog[0] > 0)
        	frog[0]-=1;
      	break;
    	case 0x00E0F06b:
      	*(LED_ptr) = 0b100; // Left arrow
      	if (frog[1] > 0)
        	frog[1]-=1;
      	break;
    	case 0x00E0F074:
      	*(LED_ptr) = 0b1000; // Right arrow
      	if (frog[1] <  numOfColumns-1)
        	frog[1]+=1;
      	break;
    	default:
      	*(LED_ptr) = 0x0; // None
  	}
}


void riverMoveFrog(int i){
  if(frog[1]!=numOfColumns-1 && frog[0]==i){
    frog[1]++;
  }
}

bool isFrogDead(){
    if(frog[0] >= numOfLanes || frog[0] < 0 || frog[1] >= numOfColumns || frog[1] < 0){
        return true;
    }
    if(Board[frog[0]][frog[1]] != safeSpace && Board[frog[0]][frog[1]] != woodenLog){
        return true;
    }
    return false;
}

void updateBoard(){
    for(int i = 0; i < numOfLanes; i++){
        if(counter % (Lanes[i].speed) == 0){
            if(Lanes[i].type == safeLane){
                continue;
            }
            else if(Lanes[i].type == leftCarLane){
                updateLeftCarLane(i);
            }
            else if(Lanes[i].type == rightCarLane){
                updateRightCarLane(i);
            }
            else if(Lanes[i].type == leftBusLane){
                updateLeftBusLane(i);
            }
            else if(Lanes[i].type == rightBusLane){
                updateRightBusLane(i);
            }
            else if(Lanes[i].type == riverLane){
                updateRiverLane(i);
                riverMoveFrog(i);
            }
        }
    }
  counter++;
  counter = counter%60;
}

void updateLeftCarLane(int i){
    for(int j = 0; j < numOfColumns - 1; j++){
        Board[i][j] = Board[i][j+1];
    }
    Board[i][numOfColumns-1] = ((rand()%(2+2*Lanes[i].density))/(1+2*Lanes[i].density));
}

void updateRightCarLane(int i){
    for(int j = numOfColumns-1; j > 0; j--){
        Board[i][j] = Board[i][j-1];
    }
    Board[i][0] = 2*((rand()%(2+2*Lanes[i].density))/(1+2*Lanes[i].density));
}

void updateLeftBusLane(int i){
    for(int j = 0; j < numOfColumns - 1; j++){
        Board[i][j] = Board[i][j+1];
    }
    if(Board[i][numOfColumns - 2] == 3 && Board[i][numOfColumns - 3] != 3){
        Board[i][numOfColumns - 1] = 3;
    }
    else if(Board[i][numOfColumns - 2] == 3 && Board[i][numOfColumns - 3] == 3){
        Board[i][numOfColumns - 1] = 0;
    }
    else{
        Board[i][numOfColumns-1] = 3*((rand()%(3+2*Lanes[i].density))/(2+2*Lanes[i].density));
    }
}

void updateRightBusLane(int i){
    for(int j = numOfColumns-1; j >0; j--){
        Board[i][j] = Board[i][j-1];
    }
    if(Board[i][1] == 4 && Board[i][2] != 4){
        Board[i][0] = 4;
    }
    else if(Board[i][1] == 4 && Board[i][2] == 4){
        Board[i][0] = 0;
    }
    else{
        Board[i][0] = 4*((rand()%(3+2*Lanes[i].density))/(2+2*Lanes[i].density));
    }
}

void updateRiverLane(int i){
    for(int j = numOfColumns-1; j >0; j--){
        Board[i][j] = Board[i][j-1];
    }
    if(Board[i][3] == 5){
        Board[i][0] = 6;
    }
    else{
        Board[i][0] = 5;
    }
}


void printBoard(){
  int start_width = 8;
  int start_height = 232;
	//plot_box(100, 100, -1);
    for(int i = numOfLanes-1; i >= 0; i--){
        for(int j = 0; j < numOfColumns; j++){
            if(i == frog[0] && j == frog[1]){
                plot_box(start_width + j * box_height, start_height - i * box_width, -1);
            }
            else{
            	plot_box(start_width + j * box_height, start_height - i * box_width, Board[i][j]);
            }
        }
    }
}

void clear_screen(){
	int x, y;
	for(x=0; x<320; x++){
		for(y=0; y<240; y++){
			plot_pixel(x,y,0);
		}	
	}
}

void plot_box(int x, int y, int type){
  	short int line_color;
  	switch(type){
    	case -1: drawFrog(x,y); return; // frog
      // case -1: line_color = GREEN; break;
    	case water: drawWater(x, y); return;
    	case woodenLog: drawLog(x, y); return;
    	case leftCar: drawLeftCar(x,y); return;
    	case rightCar: drawRightCar(x,y); return;
    	case leftBus: drawBus(x,y); return;
    	case rightBus: drawBus(x,y); return;
    	case safeSpace: 
			if(Lanes[y/16].type!=safeLane){
				drawRoad(x,y);
				return;
			}
			line_color = BLACK; break;
    	default: line_color = WHITE; break;
  	}
	plot_pixel(x, y, line_color);
  	for (int x_shift = (-box_width/2) ; x_shift<(box_width/2); x_shift++){
    	for(int y_shift = (-box_height/2); y_shift<(box_height/2); y_shift++){
   	   		plot_pixel(x+x_shift, y+y_shift, line_color );
    	}
  	}
}


void plot_pixel(int x, int y, short int line_color){
  if (x>=0 && x <= 319 && y>=0 && y<=239)
    *(short int *)(pixel_buffer_start + (y << 10) + (x << 1)) = line_color;
}


//Drawing functions

void drawRoad(int x, int y){
	for (int x_shift = (-box_width/2) ; x_shift<(box_width/2); x_shift++){
    	for(int y_shift = (-box_height/2); y_shift<(box_height/2); y_shift++){
      		if( (x_shift>-3 && x_shift<3 && y_shift>-1 && y_shift<=1) || (y_shift==(-box_height/2) || (y_shift==(box_height/2-1)))){
				plot_pixel(x+x_shift, y+y_shift, WHITE );
			}
			else{
				plot_pixel(x+x_shift, y+y_shift, BLACK );
			}
			
		}
  	}
}	

void drawBus(int x, int y){
	for (int x_shift = (-box_width/2) ; x_shift<(box_width/2); x_shift++){
    	for(int y_shift = (-box_height/2); y_shift<(box_height/2); y_shift++){
		  	if(y_shift<-1){
				if(x_shift % 4 == 0){
					plot_pixel(x+x_shift, y+y_shift, BLACK );
				}
				else{
					plot_pixel(x+x_shift, y+y_shift, WHITE );
				}
		  	}
			else if(y_shift>3 && ((x_shift>3 && x_shift<7 )|| (x_shift<-3 && x_shift>-7))){
				plot_pixel(x+x_shift, y+y_shift, GREY );
		  	}
			else{
				plot_pixel(x+x_shift, y+y_shift, OLIVE );
			}
			
		}
  	}
}	

void drawLeftCar(int x, int y){
	for (int x_shift = (-box_width/2) ; x_shift<(box_width/2); x_shift++){
    	for(int y_shift = (-box_height/2); y_shift<(box_height/2); y_shift++){
      		if(y_shift>3 && ((x_shift>3 && x_shift<7 )|| (x_shift<-3 && x_shift>-7))){
				plot_pixel(x+x_shift, y+y_shift, BLUE );
			}
			else if(y_shift<=-1 && y_shift>-6){
				if(x_shift<(box_width/2-3) && x_shift>=(-box_width/2+3))
				plot_pixel(x+x_shift, y+y_shift, WHITE );
			}
			else if(y_shift>-1 && y_shift<6){
				plot_pixel(x+x_shift, y+y_shift, WHITE );
			}
			
		}
  	}
}	
	
void drawRightCar(int x, int y){
	for (int x_shift = (-box_width/2) ; x_shift<(box_width/2); x_shift++){
    	for(int y_shift = (-box_height/2); y_shift<(box_height/2); y_shift++){
      		if(y_shift>3 && ((x_shift>3 && x_shift<7 )|| (x_shift<-3 && x_shift>-7))){
				    plot_pixel(x+x_shift, y+y_shift, ORANGE );
			    }
			else if(y_shift<=-1 && y_shift>-6){
				if(x_shift<(box_width/2-3) && x_shift>=(-box_width/2+3))
				plot_pixel(x+x_shift, y+y_shift, GREY );
			}
			else if(y_shift>-1 && y_shift<6){
				plot_pixel(x+x_shift, y+y_shift, GREY );
			}
			
		}
  	}
}

void drawFrog(int x, int y){
	for (int x_shift = (-box_width/2) ; x_shift<(box_width/2); x_shift++){
    for(int y_shift = (-box_height/2); y_shift<(box_height/2); y_shift++){
      if(y_shift >= 7 || y_shift <= -7 || x_shift >= 7 || x_shift <= -7){ // border
        plot_pixel(x+x_shift, y+y_shift, BLACK );
      }
      else if ((x_shift == -4 || x_shift == 4 || x_shift == -5 || x_shift == 5) && (y_shift == -5)){ // pupils...
        plot_pixel(x+x_shift, y+y_shift, BLACK );
      }
      else if((x_shift<=-2 || x_shift>=2) && (y_shift <= -3)){ //eyes
        plot_pixel(x+x_shift, y+y_shift, WHITE );
      }
      else if ((x_shift>=-2 && x_shift<=2) && y_shift == 0){ // mouth thing
        plot_pixel(x+x_shift, y+y_shift, BLACK );
      }
      else if ((x_shift == -4 || x_shift == 4) && y_shift >= 3 && y_shift <= 5){ // main legs
        plot_pixel(x+x_shift, y+y_shift, BLACK );
      }
      else if ((x_shift == -2 || x_shift == 2) && y_shift >= 5){ // leg... contours... 
        plot_pixel(x+x_shift, y+y_shift, BLACK );
      }
      else if ((x_shift == -5 || x_shift == 5) && y_shift == 6){ // foot 
        plot_pixel(x+x_shift, y+y_shift, BLACK );
      }
      else{ //body
        plot_pixel(x+x_shift, y+y_shift, GREEN );
      }
    }
  }  
}

void drawLog(int x, int y){
	for (int x_shift = (-box_width/2) ; x_shift<(box_width/2); x_shift++){
    for(int y_shift = (-box_height/2); y_shift<(box_height/2); y_shift++){
      if ((x_shift >= -6 && x_shift <= 6) && (y_shift == 8 || y_shift == -8 || y_shift == 6 || y_shift == -6 || y_shift ==0)){ // wood cracks
        plot_pixel(x+x_shift, y+y_shift, BROWN );
      }
      else{ //body
        plot_pixel(x+x_shift, y+y_shift, ORANGE );
      }
    }
  }  
}

void drawWater(int x, int y){
	for (int x_shift = (-box_width/2) ; x_shift<(box_width/2); x_shift++){
    for(int y_shift = (-box_height/2); y_shift<(box_height/2); y_shift++){
      if ((x_shift >= 4) && (y_shift == 3)){
        plot_pixel(x+x_shift, y+y_shift, WHITE );
      }
      else if ((x_shift <= -4) && (y_shift == -3)){
        plot_pixel(x+x_shift, y+y_shift, WHITE );
      }
      else{ //body
        plot_pixel(x+x_shift, y+y_shift, BLUE );
      }
    }
  }  
}


//Complete Level Start
void startLevel(int i){
  volatile int * hex_ptr = (int *)HEX3_HEX0_BASE;
	switch(i){
		case 1: 
      (*hex_ptr) = 0b001110000011111100000110; // L1
      initLevel1();
		  break;
		case 2: 
      (*hex_ptr) = 0b001110000011111101011011; // L2
      initLevel2();
		  break;
		case 3: 
      (*hex_ptr) = 0b001110000011111101001111; // L3
      initLevel3();
		  break;
		case 4: 
      (*hex_ptr) = 0b001110000011111101100110; // L4
      initLevel4();
		  break;
		case 5: 
      (*hex_ptr) = 0b001110000011111101101101; // L5
      initLevel5();
		  break;
		case 6: 
      (*hex_ptr) = 0b001110000011111101111101; // L6
      initLevel6();
		  break;
		case 7: 
      (*hex_ptr) = 0b001110000011111100000111; // L7
      initLevel7();
		  break;
		case 8: 
      (*hex_ptr) = 0b001110000011111101111111; // L8
      initLevel8();
		  break;
		case 9: 
      (*hex_ptr) = 0b001110000011111101101111; // L9
      initLevel9();
		  break;
		case 10: 
      (*hex_ptr) = 0b001110000000011000111111; // L10
      initLevel10();
		  break;
		default: 
      (*hex_ptr) = 0b0011100000000110; // L1      
      initLevel1();
      break;
	}
	initializeBoard();
    initializeFrog();
    clear_screen();
    wait_for_vsync();
    clear_screen();
	printBoard();
}

void runLevel(){
	 while(1){
        doDelay();
        updateBoard();
        printBoard();
        if(isFrogDead()){
            return;
        }
		if(frog[0] == numOfLanes-1){
			return;
		}
        wait_for_vsync();
	}
}

//Levels

void initLevel1(){
    Lanes[0].type = safeLane;         Lanes[0].speed = notMoving;     Lanes[0].density = zero; 
    Lanes[1].type = leftCarLane;      Lanes[1].speed = verySlow;      Lanes[1].density = low;
    Lanes[2].type = leftCarLane;      Lanes[2].speed = verySlow;          Lanes[2].density = low;
    Lanes[3].type = leftCarLane;      Lanes[3].speed = slow;        Lanes[3].density = veryLow;
    Lanes[4].type = rightCarLane;      Lanes[4].speed = slow;          Lanes[4].density = veryLow;
    Lanes[5].type = rightCarLane;      Lanes[5].speed = verySlow;      Lanes[5].density = low;
	Lanes[6].type = rightCarLane;      Lanes[6].speed = verySlow;      Lanes[6].density = veryLow;
    Lanes[7].type = leftCarLane;      Lanes[7].speed = verySlow;          Lanes[7].density = low;
    Lanes[8].type = leftCarLane;      Lanes[8].speed = slow;        Lanes[8].density = veryLow;
    Lanes[9].type = leftCarLane;      Lanes[9].speed = slow;          Lanes[9].density = veryLow;
    Lanes[10].type = rightCarLane;      Lanes[10].speed = slow;      Lanes[10].density = veryLow;
	Lanes[11].type = rightCarLane;      Lanes[11].speed = verySlow;      Lanes[11].density = low;
    Lanes[12].type = rightCarLane;      Lanes[12].speed = slow;          Lanes[12].density = low;
    Lanes[13].type = riverLane;      Lanes[13].speed = verySlow;        Lanes[13].density = low;
    Lanes[14].type = safeLane;      Lanes[14].speed = notMoving;          Lanes[14].density = zero;
}

void initLevel2(){
    Lanes[0].type = safeLane;         Lanes[0].speed = notMoving;     Lanes[0].density = zero; 
    Lanes[1].type = leftCarLane;      Lanes[1].speed = slow;      Lanes[1].density = low;
    Lanes[2].type = rightCarLane;      Lanes[2].speed = slow;          Lanes[2].density = low;
    Lanes[3].type = leftBusLane;      Lanes[3].speed = slow;        Lanes[3].density = low;
    Lanes[4].type = rightCarLane;      Lanes[4].speed = slow;          Lanes[4].density = low;
    Lanes[5].type = rightBusLane;      Lanes[5].speed = slow;      Lanes[5].density = low;
	Lanes[6].type = rightBusLane;      Lanes[6].speed = medium;      Lanes[6].density = low;
    Lanes[7].type = leftCarLane;      Lanes[7].speed = medium;          Lanes[7].density = low;
    Lanes[8].type = riverLane;      Lanes[8].speed = verySlow;        Lanes[8].density = veryLow;
    Lanes[9].type = riverLane;      Lanes[9].speed = slow;          Lanes[9].density = veryLow;
    Lanes[10].type = riverLane;      Lanes[10].speed = medium;      Lanes[10].density = veryLow;
	Lanes[11].type = rightCarLane;      Lanes[11].speed = verySlow;      Lanes[11].density = low;
    Lanes[12].type = rightBusLane;      Lanes[12].speed = slow;          Lanes[12].density = low;
    Lanes[13].type = riverLane;      Lanes[13].speed = medium;        Lanes[13].density = low;
    Lanes[14].type = safeLane;      Lanes[14].speed = notMoving;          Lanes[14].density = zero;
}

void initLevel3(){
    Lanes[0].type = safeLane;         Lanes[0].speed = notMoving;     Lanes[0].density = zero; 
    Lanes[1].type = leftBusLane;      Lanes[1].speed = medium;      Lanes[1].density = low;
    Lanes[2].type = rightBusLane;      Lanes[2].speed = slow;          Lanes[2].density = low;
    Lanes[3].type = leftBusLane;      Lanes[3].speed = slow;        Lanes[3].density = moderate;
    Lanes[4].type = rightBusLane;      Lanes[4].speed = slow;          Lanes[4].density = moderate;
    Lanes[5].type = leftBusLane;      Lanes[5].speed = medium;      Lanes[5].density = low;
	Lanes[6].type = rightBusLane;      Lanes[6].speed = slow;      Lanes[6].density = veryLow;
    Lanes[7].type = leftCarLane;      Lanes[7].speed = medium;          Lanes[7].density = low;
    Lanes[8].type = leftCarLane;      Lanes[8].speed = slow;        Lanes[8].density = veryLow;
    Lanes[9].type = leftCarLane;      Lanes[9].speed = slow;          Lanes[9].density =moderate;
    Lanes[10].type = riverLane;      Lanes[10].speed = medium;      Lanes[10].density = moderate;
	Lanes[11].type = rightCarLane;      Lanes[11].speed = medium;      Lanes[11].density = low;
    Lanes[12].type = rightCarLane;      Lanes[12].speed = medium;          Lanes[12].density = low;
    Lanes[13].type = riverLane;      Lanes[13].speed = slow;        Lanes[13].density = low;
    Lanes[14].type = safeLane;      Lanes[14].speed = notMoving;          Lanes[14].density = zero;
}

void initLevel4(){
    Lanes[0].type = safeLane;         Lanes[0].speed = notMoving;     Lanes[0].density = zero; 
    Lanes[1].type = leftCarLane;      Lanes[1].speed = medium;      Lanes[1].density = low;
    Lanes[2].type = riverLane;      Lanes[2].speed = medium;          Lanes[2].density = low;
    Lanes[3].type = leftCarLane;      Lanes[3].speed = slow;        Lanes[3].density = moderate;
    Lanes[4].type = riverLane;      Lanes[4].speed = slow;          Lanes[4].density = low;
    Lanes[5].type = rightCarLane;      Lanes[5].speed = slow;      Lanes[5].density = moderate;
	Lanes[6].type = riverLane;      Lanes[6].speed = medium;      Lanes[6].density = low;
    Lanes[7].type = leftCarLane;      Lanes[7].speed = slow;          Lanes[7].density = low;
    Lanes[8].type = leftCarLane;      Lanes[8].speed = medium;        Lanes[8].density = veryLow;
    Lanes[9].type = riverLane;      Lanes[9].speed = medium;          Lanes[9].density = low;
    Lanes[10].type = rightCarLane;      Lanes[10].speed = medium;      Lanes[10].density = moderate;
	Lanes[11].type = riverLane;      Lanes[11].speed = slow;      Lanes[11].density = moderate;
    Lanes[12].type = rightCarLane;      Lanes[12].speed = slow;          Lanes[12].density = low;
    Lanes[13].type = riverLane;      Lanes[13].speed = slow;        Lanes[13].density = moderate;
    Lanes[14].type = safeLane;      Lanes[14].speed = notMoving;          Lanes[14].density = zero;
}

void initLevel5(){
    Lanes[0].type = safeLane;         Lanes[0].speed = notMoving;     Lanes[0].density = zero; 
    Lanes[1].type = leftCarLane;      Lanes[1].speed = medium;      Lanes[1].density = low;
    Lanes[2].type = riverLane;      Lanes[2].speed = medium;          Lanes[2].density = low;
    Lanes[3].type = leftCarLane;      Lanes[3].speed = fast;        Lanes[3].density = moderate;
    Lanes[4].type = riverLane;      Lanes[4].speed = slow;          Lanes[4].density = low;
    Lanes[5].type = rightCarLane;      Lanes[5].speed = medium;      Lanes[5].density = moderate;
	Lanes[6].type = riverLane;      Lanes[6].speed = slow;      Lanes[6].density = low;
    Lanes[7].type = leftCarLane;      Lanes[7].speed = fast;          Lanes[7].density = low;
    Lanes[8].type = leftCarLane;      Lanes[8].speed = slow;        Lanes[8].density = high;
    Lanes[9].type = leftBusLane;      Lanes[9].speed = medium;          Lanes[9].density = low;
    Lanes[10].type = rightCarLane;      Lanes[10].speed = medium;      Lanes[10].density = moderate;
	Lanes[11].type = leftCarLane;      Lanes[11].speed = slow;      Lanes[11].density = moderate;
    Lanes[12].type = rightCarLane;      Lanes[12].speed = medium;          Lanes[12].density = low;
    Lanes[13].type = rightBusLane;      Lanes[13].speed = slow;        Lanes[13].density = high;
    Lanes[14].type = safeLane;      Lanes[14].speed = notMoving;          Lanes[14].density = zero;
}

void initLevel6(){
    Lanes[0].type = safeLane;         Lanes[0].speed = notMoving;     Lanes[0].density = zero; 
    Lanes[1].type = riverLane;      Lanes[1].speed = fast;      Lanes[1].density = low;
    Lanes[2].type = leftCarLane;      Lanes[2].speed = medium;          Lanes[2].density = moderate;
    Lanes[3].type = leftCarLane;      Lanes[3].speed = medium;        Lanes[3].density = moderate;
    Lanes[4].type = rightBusLane;      Lanes[4].speed = slow;          Lanes[4].density = low;
    Lanes[5].type = rightCarLane;      Lanes[5].speed = medium;      Lanes[5].density = moderate;
	Lanes[6].type = rightCarLane;      Lanes[6].speed = fast;      Lanes[6].density = low;
    Lanes[7].type = leftCarLane;      Lanes[7].speed = medium;          Lanes[7].density = high;
    Lanes[8].type = rightCarLane;      Lanes[8].speed = fast;        Lanes[8].density = low;
    Lanes[9].type = leftBusLane;      Lanes[9].speed = medium;          Lanes[9].density = high;
    Lanes[10].type = rightCarLane;      Lanes[10].speed = fast;      Lanes[10].density = moderate;
	Lanes[11].type = riverLane;      Lanes[11].speed = slow;      Lanes[11].density = moderate;
    Lanes[12].type = rightCarLane;      Lanes[12].speed = medium;          Lanes[12].density = moderate;
    Lanes[13].type = riverLane;      Lanes[13].speed = slow;        Lanes[13].density = moderate;
    Lanes[14].type = safeLane;      Lanes[14].speed = notMoving;          Lanes[14].density = zero;
}

void initLevel7(){
    Lanes[0].type = safeLane;         Lanes[0].speed = notMoving;     Lanes[0].density = zero; 
    Lanes[1].type = leftCarLane;      Lanes[1].speed = fast;      Lanes[1].density = high;
    Lanes[2].type = riverLane;      Lanes[2].speed = medium;          Lanes[2].density = high;
    Lanes[3].type = leftCarLane;      Lanes[3].speed = medium;        Lanes[3].density = moderate;
    Lanes[4].type = riverLane;      Lanes[4].speed = medium;          Lanes[4].density = moderate;
    Lanes[5].type = rightCarLane;      Lanes[5].speed = medium;      Lanes[5].density = moderate;
	Lanes[6].type = riverLane;      Lanes[6].speed = fast;      Lanes[6].density = high;
    Lanes[7].type = leftCarLane;      Lanes[7].speed = fast;          Lanes[7].density = high;
    Lanes[8].type = leftCarLane;      Lanes[8].speed = medium;        Lanes[8].density = moderate;
    Lanes[9].type = riverLane;      Lanes[9].speed = fast;          Lanes[9].density = high;
    Lanes[10].type = rightCarLane;      Lanes[10].speed = medium;      Lanes[10].density = moderate;
	Lanes[11].type = riverLane;      Lanes[11].speed = fast;      Lanes[11].density = moderate;
    Lanes[12].type = rightCarLane;      Lanes[12].speed = medium;          Lanes[12].density = moderate;
    Lanes[13].type = riverLane;      Lanes[13].speed = medium;        Lanes[13].density = high;
    Lanes[14].type = safeLane;      Lanes[14].speed = notMoving;          Lanes[14].density = zero;
}

void initLevel8(){
    Lanes[0].type = safeLane;         Lanes[0].speed = notMoving;     Lanes[0].density = moderate; 
    Lanes[1].type = leftCarLane;      Lanes[1].speed = fast;      Lanes[1].density = high;
    Lanes[2].type = leftCarLane;      Lanes[2].speed = medium;          Lanes[2].density = moderate;
    Lanes[3].type = leftCarLane;      Lanes[3].speed = fast;        Lanes[3].density = moderate;
    Lanes[4].type = riverLane;      Lanes[4].speed = fast;          Lanes[4].density = high;
    Lanes[5].type = rightCarLane;      Lanes[5].speed = fast;      Lanes[5].density = moderate;
	Lanes[6].type = rightBusLane;      Lanes[6].speed = veryFast;      Lanes[6].density = high;
    Lanes[7].type = leftCarLane;      Lanes[7].speed = veryFast;          Lanes[7].density = moderate;
    Lanes[8].type = leftCarLane;      Lanes[8].speed = fast;        Lanes[8].density = veryHigh;
    Lanes[9].type = riverLane;      Lanes[9].speed = medium;          Lanes[9].density = high;
    Lanes[10].type = rightCarLane;      Lanes[10].speed = medium;      Lanes[10].density = high;
	Lanes[11].type = riverLane;      Lanes[11].speed = fast;      Lanes[11].density = moderate;
    Lanes[12].type = rightCarLane;      Lanes[12].speed = veryFast;          Lanes[12].density = moderate;
    Lanes[13].type = riverLane;      Lanes[13].speed = fast;        Lanes[13].density = moderate;
    Lanes[14].type = safeLane;      Lanes[14].speed = notMoving;          Lanes[14].density = zero;
}

void initLevel9(){
    Lanes[0].type = safeLane;         Lanes[0].speed = notMoving;     Lanes[0].density = zero; 
    Lanes[1].type = rightCarLane;      Lanes[1].speed = fast;      Lanes[1].density = high;
    Lanes[2].type = leftCarLane;      Lanes[2].speed = veryFast;          Lanes[2].density = veryHigh;
    Lanes[3].type = leftBusLane;      Lanes[3].speed = fast;        Lanes[3].density = veryHigh;
    Lanes[4].type = riverLane;      Lanes[4].speed = fast;          Lanes[4].density = high;
    Lanes[5].type = riverLane;      Lanes[5].speed = veryFast;      Lanes[5].density = veryHigh;
	Lanes[6].type = leftCarLane;      Lanes[6].speed = veryFast;      Lanes[6].density = high;
    Lanes[7].type = riverLane;      Lanes[7].speed = veryFast;          Lanes[7].density = veryHigh;
    Lanes[8].type = leftCarLane;      Lanes[8].speed = fast;        Lanes[8].density = high;
    Lanes[9].type = leftBusLane;      Lanes[9].speed = fast;          Lanes[9].density = high;
    Lanes[10].type = rightCarLane;      Lanes[10].speed = veryFast;      Lanes[10].density = veryHigh;
	Lanes[11].type = leftBusLane;      Lanes[11].speed = veryFast;      Lanes[11].density = high;
    Lanes[12].type = rightCarLane;      Lanes[12].speed = veryFast;          Lanes[12].density = high;
    Lanes[13].type = riverLane;      Lanes[13].speed = fast;        Lanes[13].density = veryHigh;
    Lanes[14].type = safeLane;      Lanes[14].speed = notMoving;          Lanes[14].density = zero;
}

void initLevel10(){
    Lanes[0].type = safeLane;         Lanes[0].speed = notMoving;     Lanes[0].density = zero; 
    Lanes[1].type = leftCarLane;      Lanes[1].speed = veryFast;      Lanes[1].density = veryHigh;
    Lanes[2].type = riverLane;      Lanes[2].speed = veryFast;          Lanes[2].density = veryHigh;
    Lanes[3].type = leftBusLane;      Lanes[3].speed = veryFast;        Lanes[3].density = veryHigh;
    Lanes[4].type = rightBusLane;      Lanes[4].speed = veryFast;          Lanes[4].density = veryHigh;
    Lanes[5].type = riverLane;      Lanes[5].speed = fast;      Lanes[5].density = veryHigh;
	Lanes[6].type = riverLane;      Lanes[6].speed = veryFast;      Lanes[6].density = veryHigh;
    Lanes[7].type = leftCarLane;      Lanes[7].speed = veryFast;          Lanes[7].density = veryHigh;
    Lanes[8].type = leftBusLane;      Lanes[8].speed = veryFast;        Lanes[8].density = veryHigh;
    Lanes[9].type = riverLane;      Lanes[9].speed = veryFast;          Lanes[9].density = veryHigh;
    Lanes[10].type = leftBusLane;      Lanes[10].speed = veryFast;      Lanes[10].density = veryHigh;
	Lanes[11].type = riverLane;      Lanes[11].speed = veryFast;      Lanes[11].density = veryHigh;
    Lanes[12].type = rightCarLane;      Lanes[12].speed = veryFast;          Lanes[12].density = veryHigh;
    Lanes[13].type = leftCarLane;      Lanes[13].speed = veryFast;        Lanes[13].density = veryHigh;
    Lanes[14].type = safeLane;      Lanes[14].speed = notMoving;          Lanes[14].density = zero;
}

void youWin(){
	clear_screen();
	int start_width = 8;
  	int start_height = 232;
    for(int i = numOfLanes-1; i >= 0; i--){
        for(int j = 0; j < numOfColumns; j++){
            if((i == 8 && j == 8)||(i == 9 && j == 8)||(i == 10 && j == 8)||(i == 11 && j == 8)||(i == 12 && j == 8)||(i == 8 && j == 9)||(i == 8 && j == 10)||(i == 8 && j == 11)||(i == 9 && j == 11)||(i == 10 && j == 11)||(i == 11 && j == 11)||(i == 12 && j == 11)){
                plot_box(start_width + j * box_width, start_height - i * box_height, -2);
			}
			else if((i == 6 && j == 2)||(i == 5 && j == 2)||(i == 4 && j == 2)||(i == 3 && j == 2)||(i == 2 && j == 2)||(i == 2 && j == 3)||(i == 2 && j == 4)||(i == 3 && j == 4)||(i == 4 && j == 4)||(i == 2 && j == 5)||(i == 2 && j == 6)||(i == 3 && j == 6)||(i == 4 && j == 6)||(i == 5 && j == 6)||(i == 6 && j == 6)){
				plot_box(start_width + j * box_width, start_height - i * box_height, -2);
			}
			else if((i == 6 && j == 9)||(i == 5 && j == 9)||(i == 4 && j == 9)||(i == 3 && j == 9)||(i == 2 && j == 9)){
				plot_box(2 * start_width + j * box_width, start_height - i * box_height, -2);
			}
			else if((i == 6 && j == 13)||(i == 5 && j == 13)||(i == 4 && j == 13)||(i == 3 && j == 13)||(i == 2 && j == 13)||(i == 5 && j == 14)||(i == 4 && j == 15)||(i == 3 && j == 16)||(i == 6 && j == 17)||(i == 5 && j == 17)||(i == 4 && j == 17)||(i == 3 && j == 17)||(i == 2 && j == 17)){
				plot_box(start_width + j * box_width, start_height - i * box_height, -2);
			}
        }
    }
}