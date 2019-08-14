/*
 * nes-mini - NES Mini Driver for the RetroPie.
 *
 * (c) Copyright 2019  Joaquim (joaquim.org@gmail.com)
 *
 * Permission to use, copy, modify and distribute nes-mini in both binary and
 * source form, for non-commercial purposes, is hereby granted without fee,
 * providing that this license information and copyright notice appear with
 * all copies and any derived work.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event shall the authors be held liable for any damages
 * arising from the use of this software.
 *
 * nes-mini is freeware for PERSONAL USE only. Commercial users should
 * seek permission of the copyright holders first. Commercial use includes
 * charging money for nes-mini or software derived from nes-mini.
 *
 * The copyright holders request that bug fixes and improvements to the code
 * should be forwarded to them so everyone can benefit from the modifications
 * in future versions.
 *
 * Raspberry Pi is a trademark of the Raspberry Pi Foundation.
 *
 *	Original code from https://gist.github.com/jnewc/f8b668c41d7d4a68f6e46f46e8c559c2
 */

#include <linux/slab.h>			/* kzalloc */
#include <linux/module.h>   	/* Needed by all modules */
#include <linux/kernel.h>   	/* KERN_INFO */
#include <linux/timer.h>		/* timer_list */
#include <linux/workqueue.h>	/* schedule_work */

#include <linux/input.h>
#include <linux/i2c.h>

#define REFRESH_RATE_MSECS		20

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joaquim Pereira");
MODULE_DESCRIPTION("NES Mini Controler");
MODULE_VERSION("0.1");

struct pad_state {
	signed int primary_x;
	signed int primary_y;
};

struct pad_data {
	struct input_dev *device;
	struct pad_state *pad_state;
};

struct pad_data* pad_data;


static const int max_nes_buttons = 4;

// 	 U,      D,        L,        R,         
// A,     B,     X,     Y,     Start, Select
static const short nes_btn[] = {
	BTN_A, BTN_B, BTN_START, BTN_SELECT
};

/**********************************************************************
 * i2c setup
 **********************************************************************/

struct i2c_adapter* i2c_dev;
struct i2c_client* i2c_client;

static struct timer_list i2c_timer;

static struct i2c_board_info __initdata board_info[] =  {
	{
		I2C_BOARD_INFO("nes-mini", 0x52),
	}
};


static void i2c_work_handler(struct work_struct* work) 
{
	int j;
    s32 I2C_data[8];
    unsigned char buttons[21];

    for (j=0; j<21; j++){        
        buttons[j]=0;
    }

    // read raw controller data starting at the beginning
    if (i2c_smbus_write_byte(i2c_client, 0x00) < 0 ) {
        pr_err("I2C Erro");
    }
            
    for (j=0; j<8; j++) {
        I2C_data[j] = i2c_smbus_read_byte(i2c_client);     
    }

    /*if ( I2C_data[6] < 255 || I2C_data[7] < 255 ) {
        pr_err("I2C | %d : %d", I2C_data[6], I2C_data[7]);
    }*/

    // parse the controller data
    // digital controls: U, D, L, R, A, B, X, Y, Start, Select
    buttons[0] = ~(I2C_data[7]) & 0b00000001;			//U
    buttons[1] = ~((I2C_data[6] >> 6)) & 0b00000001;	//D
    buttons[2] = ~((I2C_data[7] >> 1)) & 0b00000001;	//L
    buttons[3] = ~((I2C_data[6] >> 7)) & 0b00000001;	//R

    buttons[4] = ~((I2C_data[7] >> 4)) & 0b00000001;	//A
    buttons[5] = ~((I2C_data[7] >> 6)) & 0b00000001;	//B

    buttons[6] = ~((I2C_data[6] >> 2)) & 0b00000001;	//Start
    buttons[7] = ~((I2C_data[6] >> 4)) & 0b00000001;	//Select


    input_report_abs(pad_data->device, ABS_Y, !buttons[0]-!buttons[1]);
    input_report_abs(pad_data->device, ABS_X, !buttons[2]-!buttons[3]);

    for (j=0; j < max_nes_buttons; j++){
        input_report_key(pad_data->device, nes_btn[j], buttons[j + 4]);
    }

	input_sync(pad_data->device);
}
DECLARE_WORK(i2c_work, i2c_work_handler);


static void i2c_timer_callback( unsigned long data )
{	
	schedule_work(&i2c_work);
	
	mod_timer(&i2c_timer, jiffies + msecs_to_jiffies(REFRESH_RATE_MSECS) ); 
}

/**********************************************************************
 * Device setup
 **********************************************************************/

static int setup_device(void) 
{
	int result = 0, i;
	
	// Allocate memory to store our pad data
	pad_data = kzalloc(sizeof (struct pad_data), GFP_KERNEL);
	
	// Create the device
	pad_data->device = input_allocate_device();
	if (!pad_data->device) {
		printk(KERN_ERR "enough memory\n");
		return -1;
	}

	// Setup device description
	pad_data->device->name = "NES Mini Controller";
	pad_data->device->phys = "input1";

	pad_data->device->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);

	// Setup analog sticks
    input_set_abs_params(pad_data->device, ABS_X, -1, 1, 0, 0);
    input_set_abs_params(pad_data->device, ABS_Y, -1, 1, 0, 0);
	
	for (i = 0; i < max_nes_buttons; i++) {
		__set_bit(nes_btn[i], pad_data->device->keybit);
    }

	result = input_register_device(pad_data->device);
	if (result < 0) {
		printk(KERN_ERR "Failed to register device\n");
	}

	return result;
}

/**********************************************************************
 * Driver lifecycle
 **********************************************************************/

int nes_mini_init(void)
{
	s32 value;
	int result = 0, j;

	printk(KERN_INFO "loading NES Mini driver v0.1.\n");
	
	result = setup_device();
	if(result < 0) {
		printk(KERN_INFO "FAIL: Could not setup device. Result: %d", result);
		input_free_device(pad_data->device);
		goto finish;
	}   
    
	// Setup device
	i2c_dev = i2c_get_adapter(1);
	i2c_client = i2c_new_device(i2c_dev, board_info);

	// Setup timer
	setup_timer(&i2c_timer, i2c_timer_callback, 0);
	result = mod_timer(&i2c_timer, jiffies + msecs_to_jiffies(REFRESH_RATE_MSECS) );
	
	if(result < 0) {
		printk(KERN_INFO "FAIL: Timer not setup. Result: %d", result);
		goto finish;
	}
	
	// Initialise with control call to set mode as output
	//udelay(1000);
    i2c_smbus_write_byte_data(i2c_client, 0xF0, 0x55);
    //udelay(1000);
    i2c_smbus_write_byte_data(i2c_client, 0xFB, 0x00);

    i2c_smbus_write_byte(i2c_client, 0x00);

    printk(KERN_INFO "I2C Read :\n");
    for (j=0; j<8; j++) {
        value = i2c_smbus_read_byte(i2c_client);
        printk(KERN_INFO "%d : %d\n", j, value);
    }
    
	/* 
     * A non 0 return means init_module failed; module can't be loaded. 
     */
finish:
	return result;
}

void nes_mini_exit(void)
{
    printk(KERN_INFO "Close NES Mini driver...\n");
    
	input_unregister_device(pad_data->device);
	input_free_device(pad_data->device);
    
	i2c_unregister_device(i2c_client);
    
	del_timer( &i2c_timer );
	
	kzfree(pad_data);
}

module_init(nes_mini_init);
module_exit(nes_mini_exit);