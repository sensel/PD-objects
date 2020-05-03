#include <stdio.h>
#include <string.h>
#include <m_pd.h>
#include "g_canvas.h"

#include <unistd.h>
#include <stdlib.h>
#include "sensel.h"
#include "sensel_device.h"

#define DEBUG(x)

/**
  This is the Sensel Pd object, written by Rachel Hachem 
  under the guidance of Dr. Ivica Ico Bukvic.
  
  Its functionalities are as follows:
  - When sent a bang, it polls the Sensel for data and 
    outputs it as a list, with each contact being a
    separate list.
  - When sent a "discover" message, it will look for the
    first available Sensel device and connect to it.
  - When sent a "connect" message with a serial number as
    the argument, it will find that Sensel device and
    connect to it.
*/

static t_class *sensel_class;

typedef struct _sensel
{
	t_object x_obj;
	t_atom x_atom;
	t_canvas *x_canvas;

	t_symbol *x_patch_name;
	t_symbol *x_patch_path;
	t_symbol *x_remote_name;

	t_outlet *x_outlet;

  SENSEL_HANDLE x_handle;
  SenselSensorInfo x_sensor_info;
  SenselFrameData *x_frame;
  int x_connected;

} t_sensel;

// Forward declarations
void sensel_poll(t_sensel *x);

/**
    Handles a bang input; polls the Sensel
*/
static void sensel_bang(t_sensel *x)
{
    sensel_poll(x);
}

/**
  Connects the Pd patch to a specific Sensel device, using
  the serial number as an argument.
*/
void sensel_connect(t_sensel *x, t_symbol *s)
{
	post("Connecting with serial number %s...", s->s_name);

    if (x->x_connected == 1)
    {
        post("Sensel device already connected, please disconnect before trying to connect again.");
        return;
    }

    //List of all available Sensel devices
    SenselDeviceList list;

    //Get a list of available Sensel devices
    senselGetDeviceList(&list);
    if (list.num_devices == 0)
    {
        post("No Sensel device found");
        return;
    }

    int result = senselOpenDeviceBySerialNum(&x->x_handle, (unsigned char *)s->s_name);
    
    if (!result)
    {
        //Get the sensor info
        senselGetSensorInfo(x->x_handle, &x->x_sensor_info);
        //Set the frame content to scan contact data
        senselSetFrameContent(x->x_handle, FRAME_CONTENT_CONTACTS_MASK);
        //Allocate a frame of data, must be done before reading frame data
        senselAllocateFrameData(x->x_handle, &x->x_frame);
        post("Width: %fmm", x->x_sensor_info.width);
        post("Height: %fmm", x->x_sensor_info.height);
        post("Cols: %d", x->x_sensor_info.num_cols);
        post("Rows: %d", x->x_sensor_info.num_rows);
        x->x_connected = 1;
    }

    if (x->x_connected != 1) {
    	error("Failed to find the Sensel Morph with a serial number %s.", s->s_name);
    }
}

/**
  Does not connect anything. It outputs a list of 
  all the connected Sensels' serial numbers, so that the user
  can then use the "connect" message with the serial number of 
  the desired Sensel.
*/
void sensel_discover(t_sensel *x)
{

    if (x->x_connected == 1)
    {
        post("Sensel device already connected, please disconnect before trying to connect again.");
        return;
    }
    
    //List of all available Sensel devices
    SenselDeviceList list;

    //Get a list of available Sensel devices
    senselGetDeviceList(&list);
    if (list.num_devices == 0)
    {
        post("No Sensel device found");
        return;
    }
 
    //Open a Sensel device by the id in the SenselDeviceList, handle initialized 
    senselOpenDeviceByID(&x->x_handle, list.devices[0].idx);
     
    //Get the sensor info
    senselGetSensorInfo(x->x_handle, &x->x_sensor_info);
    
    //Set the frame content to scan contact data
    senselSetFrameContent(x->x_handle, FRAME_CONTENT_CONTACTS_MASK);
    
    //Allocate a frame of data, must be done before reading frame data
    senselAllocateFrameData(x->x_handle, &x->x_frame);
    
    //Post information to the Pd console
    post("Width: %fmm", x->x_sensor_info.width);
    post("Height: %fmm", x->x_sensor_info.height);
    post("Cols: %d", x->x_sensor_info.num_cols);
    post("Rows: %d", x->x_sensor_info.num_rows);
    x->x_connected = 1;
}

/**
  Polls for the Sensel contact data. Outputs a list for every
  current contact, each comprised of 19 data points, listed
  clearly in the code.
*/
void sensel_poll(t_sensel *x) {

    if (x->x_connected == 1) {

        t_atom contact;
        t_atom orientation;
        t_atom major_axis;
        t_atom minor_axis;
        t_atom delta_x;
        t_atom delta_y;
        t_atom delta_force;
        t_atom delta_area;
        t_atom min_x;
        t_atom min_y;
        t_atom max_x;
        t_atom max_y;
        t_atom peak_x;
        t_atom peak_y;
        t_atom peak_force;
        t_atom x_pos;
        t_atom y_pos;
        t_atom total_force;
        t_atom area;

        t_atom ap[19];	

        unsigned int num_frames = 0;

        //Start scanning the Sensel device
        senselStartScanning(x->x_handle);

        //Read all available data from the Sensel device
        senselReadSensor(x->x_handle);

        senselGetNumAvailableFrames(x->x_handle, &num_frames);

        senselGetSensorInfo(x->x_handle, &x->x_sensor_info);

        

        for (unsigned int f = 0; f < num_frames; f++)
        {

            //Read one frame of data
            senselGetFrame(x->x_handle, x->x_frame);

            for (int c = 0; c < x->x_frame->n_contacts; c++) {
                
                //Allow all contents to be seen
                senselSetContactsMask(x->x_handle, 0x0F);

                float whichContact = (float)c;
                SETFLOAT(&contact, whichContact+1);
                SETFLOAT(&orientation, x->x_frame->contacts[c].orientation);
                SETFLOAT(&major_axis, x->x_frame->contacts[c].major_axis);
                SETFLOAT(&minor_axis, x->x_frame->contacts[c].minor_axis);
                SETFLOAT(&delta_x, x->x_frame->contacts[c].delta_x);
                SETFLOAT(&delta_y, x->x_frame->contacts[c].delta_y);
                SETFLOAT(&delta_force, x->x_frame->contacts[c].delta_force);
                SETFLOAT(&delta_area, x->x_frame->contacts[c].delta_area);
                SETFLOAT(&min_x, x->x_frame->contacts[c].min_x);
                SETFLOAT(&min_y, x->x_frame->contacts[c].min_y);
                SETFLOAT(&max_x,x->x_frame->contacts[c].max_x);
                SETFLOAT(&max_y,x->x_frame->contacts[c].max_y);
                SETFLOAT(&peak_x,x->x_frame->contacts[c].peak_x);
                SETFLOAT(&peak_y,x->x_frame->contacts[c].peak_y);
                SETFLOAT(&peak_force,x->x_frame->contacts[c].peak_force);
                SETFLOAT(&x_pos,x->x_frame->contacts[c].x_pos);
                SETFLOAT(&y_pos,x->x_frame->contacts[c].y_pos);
                SETFLOAT(&total_force,x->x_frame->contacts[c].total_force);
                SETFLOAT(&area,x->x_frame->contacts[c].area);

                ap[0] = contact;
                ap[1] = orientation;
                ap[2]  = major_axis;
                ap[3]  = minor_axis;
                ap[4]  = delta_x;
                ap[5]  = delta_y;
                ap[6]  = delta_force;
                ap[7]  = delta_area;
                ap[8]  = min_x;
                ap[9]  = min_y;
                ap[10]  = max_x;
                ap[11] = max_y;
                ap[12] = peak_x;
                ap[13] = peak_y;
                ap[14] = peak_force;
                ap[15] = x_pos;
                ap[16] = y_pos;
                ap[17] = total_force;
                ap[18] = area;
				
                outlet_list(x->x_outlet, gensym("list"), 19, ap);
            }
        }

        senselStopScanning(x->x_handle);
    }
    else {
        post("You must connect your Sensel first, using a 'connect' message!");
    }
}

/**
    Constructor for the sensel object
*/
static void *sensel_new(t_symbol *s, int argc, t_atom *argv)
{
	t_atom a;
	if (argc == 0)
	{
		argc = 1;
		SETFLOAT(&a, 0);
		argv = &a;
	}

	t_sensel *x    = (t_sensel *)pd_new(sensel_class);
	x->x_atom      = *argv;
	t_glist *glist = (t_glist *)canvas_getcurrent(); 
	x->x_canvas    = (t_canvas *)glist_getcanvas(glist);


	if (argv->a_type == A_FLOAT)
	{
		int depth=(int)atom_getint(&x->x_atom);

		if(depth<0)depth=0;
		while(depth && x->x_canvas->gl_owner) {
			x->x_canvas=x->x_canvas->gl_owner;
			depth--;
		}
	}
	else
	{
		x->x_remote_name = (t_symbol *)atom_getsymbol(&x->x_atom);
	}

    x->x_outlet = outlet_new(&x->x_obj, &s_list);

    x->x_connected = 0;
    x->x_frame = NULL;

	return(x);
}

/**
    Init sensel object
*/
void sensel_setup(void)
{
	sensel_class = class_new(gensym("sensel"), 
		(t_newmethod)sensel_new, NULL, 
		sizeof(t_sensel), 0, A_GIMME, 0);

	class_addbang(sensel_class, (t_method)sensel_bang);
	class_addmethod(sensel_class, (t_method)sensel_connect,
        gensym("connect"), A_SYMBOL, 0);
    class_addmethod(sensel_class, (t_method)sensel_discover,
        gensym("discover"), 0);
    class_addmethod(sensel_class, (t_method)sensel_poll,
        gensym("poll"), 0);
}
