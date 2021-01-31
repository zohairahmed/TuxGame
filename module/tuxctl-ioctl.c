/* tuxctl-ioctl.c
 *
 * Driver (skeleton) for the mp2 tuxcontrollers for ECE391 at UIUC.
 *
 * Mark Murphy 2006
 * Andrew Ofisher 2007
 * Steve Lumetta 12-13 Sep 2009
 * Puskar Naha 2013
 */

#include <asm/current.h>
#include <asm/uaccess.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/miscdevice.h>
#include <linux/kdev_t.h>
#include <linux/tty.h>
#include <linux/spinlock.h>

#include "tuxctl-ld.h"
#include "tuxctl-ioctl.h"
#include "mtcp.h"

#define NUM_LEDS      4
#define HEX_LENGTH 4
#define DEC_FLAG_OFFSET 4
#define GEN_MASK 0xF
#define CLEARED 0xF0000
#define debug(str, ...) \
	printk(KERN_DEBUG "%s: " str, __FUNCTION__, ## __VA_ARGS__)

// static spinlock_t tux_lock;
static unsigned char press = 255;
// static unsigned int flags;
static unsigned char just_reset = 0;
static unsigned long last_leds = 0xF0000;
static unsigned int acknowledge = 0;

static unsigned char led_vals[16] =
{
	0xe7, 0x06, 0xcb, 0x8f, 0x2e, 0xad, 0xed, 0x86,
	0xef, 0xae, 0xee, 0x6d, 0xe1, 0x4f, 0xe9, 0xe8
};

static void init_tux(struct tty_struct* tty);
static int tux_button_handle(unsigned long arg);
static int set_leds(struct tty_struct* tty, unsigned long arg);
static void clear(struct tty_struct* tty);
/************************ Protocol Implementation *************************/



/*
 * clear
 *   DESCRIPTION: Displays all 0s and no decimals on LEDS
 *   INPUTS: tty - acts as middleman between ioctls and controller
 *   OUTPUTS: none
 *   RETURN VALUE: none
 *   SIDE EFFECTS:
 */
void clear(struct tty_struct* tty){
	set_leds(tty, CLEARED);
}


/* tuxctl_handle_packet()
 * IMPORTANT : Read the header for tuxctl_ldisc_data_callback() in
 * tuxctl-ld.c. It calls this function, so all warnings there apply
 * here as well.
 * Description: Takes data from controller and tells kernel code how to process
 * Inputs: tty - tty - acts as middleman between ioctls and controller
 *				 packet - contains data sent from controller
 * Output: None
 * Return value: None
 * Side effects: None
 */
void tuxctl_handle_packet (struct tty_struct* tty, unsigned char* packet)
{
    unsigned a, b, c, c_left, c_down;
		unsigned char down_offset = 0x04;
		unsigned char left_offset = 0x02;
		unsigned char right_up_bitmask = 0x09;

    a = packet[0]; /* Avoid printk() sign extending the 8-bit */
    b = packet[1]; /* values when printing them. */
    c = packet[2];


		if (a == MTCP_BIOC_EVENT)
		{
			c_left = c & left_offset;
			c_left = c_left << 1;
			c_down = c & down_offset;
			c_down = c_down >> 1;
			c &= right_up_bitmask;
			c |= c_down;
			c |= c_left;
			press = (c << HEX_LENGTH) | (b & GEN_MASK);
			// no press = ff
			// c = f7
			// b = fb
			// a = fd
			// start = fe
			// right = 7f
			// up = ef
			// down = df
			// left = bf
		}
		if (a == MTCP_RESET)
		{
			init_tux(tty);
			just_reset = 1; // need time for tux controller to finish processing data
			// transfer
		}
		if (a == MTCP_ACK && just_reset)
		{
			acknowledge = 1;
			set_leds(tty, last_leds);
			just_reset = 0;

		}
		// actual data in b and c, idk what a is lol
		// this is for taking data from controller for processing
		// cover reset and some button handling here with the packet
		// what to do with mtcp_ack?


}

/******** IMPORTANT NOTE: READ THIS BEFORE IMPLEMENTING THE IOCTLS ************
 *                                                                            *
 * The ioctls should not spend any time waiting for responses to the commands *
 * they send to the controller. The data is sent over the serial line at      *
 * 9600 BAUD. At this rate, a byte takes approximately 1 millisecond to       *
 * transmit; this means that there will be about 9 milliseconds between       *
 * the time you request that the low-level serial driver send the             *
 * 6-byte SET_LEDS packet and the time the 3-byte ACK packet finishes         *
 * arriving. This is far too long a time for a system call to take. The       *
 * ioctls should return immediately with success if their parameters are      *
 * valid.                                                                     *
 *                                                                            *
 ******************************************************************************/
 /*
  * tuxctl_ioctl
  *   DESCRIPTION: Dispatcher function for other ioctls.
  *   INPUTS: tty - acts as middleman between ioctls and controller
	* 					file - unused
	* 					cmd - tells function which ioctl should take over
	* 					arg - additional argument in case an ioctl needs it
  *   OUTPUTS: none
  *   RETURN VALUE: 0 if successful, -EINVAL if error occurs
  *   SIDE EFFECTS: none
  */
int
tuxctl_ioctl (struct tty_struct* tty, struct file* file,
	      unsigned cmd, unsigned long arg)
{
    switch (cmd) {
	case TUX_INIT:
											init_tux(tty);
											clear(tty);
											return 0;
	case TUX_BUTTONS:
											return tux_button_handle(arg);

	case TUX_SET_LED:
											return set_leds(tty, arg);
	// dont need to do the last 3
	// case TUX_LED_ACK:
	// case TUX_LED_REQUEST:
	// case TUX_READ_LED:
	default:
	    return -EINVAL;
    }
}
//
//

/*
 * init_tux
 *   DESCRIPTION: Initializes controller to take data from ioctls
 *   INPUTS: tty - acts as middleman between ioctls and controller
 *   OUTPUTS: none
 *   RETURN VALUE: none
 *   SIDE EFFECTS: none
 */
void init_tux(struct tty_struct* tty){

		unsigned char signals_to_pass[1];
		// spin_lock_init(&tux_lock);

		signals_to_pass[0] = MTCP_LED_USR;
		tuxctl_ldisc_put(tty, signals_to_pass, 1);

		signals_to_pass[0] = MTCP_BIOC_ON;
		// spin_lock(&tux_lock);
		tuxctl_ldisc_put(tty, signals_to_pass, 1);
		// spin_unlock(&tux_lock);
}

/*
 * tux_button_handle
 *   DESCRIPTION: sends button press to userspace for button mapping
 *   INPUTS: arg - contains address to send button press too
 *   OUTPUTS: none
 *   RETURN VALUE: 0 if successful, -EINVAL if not
 *   SIDE EFFECTS: none
 */
int tux_button_handle(unsigned long arg){
	// spin_lock(&tux_lock);
	if (!arg || copy_to_user((int *) arg, &press, 1))
	{
		// spin_unlock(&tux_lock);
		return -EINVAL;
	}
	// spin_unlock(&tux_lock);
	return 0;
}

/*
 * set_leds
 *   DESCRIPTION: Formats data to be written to controller to display LEDS
 *   INPUTS: tty - acts as middleman between ioctls and controller
 * 					 arg - contains address to send button press too
 *   OUTPUTS: none
 *   RETURN VALUE: none
 *   SIDE EFFECTS: could fill up controller buffer
 */
int set_leds(struct tty_struct* tty, unsigned long arg)
{
		unsigned long temp = arg;
		int i;
		int index = 2; // we send MTCP_LED_SET and display flags
		unsigned char hex_vals[NUM_LEDS];
		unsigned char led_on[NUM_LEDS];
		unsigned char dec_points[NUM_LEDS];
		unsigned char leds_to_write[6]; // 4 bytes for LED values, 1 for MTCP_LED_SET
		// and 1 for display flags

		if (!acknowledge)
			return -1;	// don't send too many requests to the controller

		last_leds = arg;
		for (i = 0; i < NUM_LEDS; i++){
			hex_vals[i] = led_vals[temp&0xF];
			temp = temp >> HEX_LENGTH;
		}

		for (i = 0; i < NUM_LEDS; i++){
			led_on[i] = temp&0x1;
			temp = temp >> 1;
		}

		temp = temp >> HEX_LENGTH; // 4 bits between display flags and decimals

		for (i = 0; i < NUM_LEDS; i++){
			dec_points[i] = temp&0x1;
			temp = temp >> 1;
			dec_points[i] = dec_points[i] << DEC_FLAG_OFFSET;
		}

		for (i = 0; i < NUM_LEDS; i++){
			hex_vals[i] |= dec_points[i];
		}

		leds_to_write[0] = MTCP_LED_SET;
		leds_to_write[1] = GEN_MASK; // write to all LEDS

		for (i = 0; i < NUM_LEDS; i++){
			if (led_on[i])
			{
					leds_to_write[index] = hex_vals[i];
			}
			index++;
		}
		// spin_lock(&tux_lock);
		tuxctl_ldisc_put(tty, leds_to_write, index);
		// spin_unlock(&tux_lock);
		return 0;
}
