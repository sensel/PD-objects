#include <stdio.h>
#include <string.h>
#include <m_pd.h>
#include "g_canvas.h"

#include <stdlib.h>
#include "src/sensel.h"
#include "src/sensel_device.h"

#define DEBUG(x)

static t_class *sensel_class;
//static t_canvas *canvas;

/**
	SENSEL PD OBJECT
	----------------
	- CURRENTLY: Can only do things found in the Sensel examples
	- On bang, displays the Sensel objects found (example 1)
	- On integer, displays the number of contacts found (example 2)
	- On  
*/

typedef struct _sensel
{
	t_object x_obj;
	t_atom x_atom;
	t_canvas *x_canvas;

	t_symbol *x_patch_name;
	t_symbol *x_patch_path;
	t_symbol *x_remote_name;

	t_outlet *x_outlet_path;
	t_outlet *x_outlet_name;

    SENSEL_HANDLE x_handle;
    SenselSensorInfo x_sensor_info;
    SenselFrameData *x_frame;
    int x_connected;

} t_sensel;

/**
    Handles a bang input
*/
static void sensel_bang(t_sensel *x)
{
	char buf[MAXPDSTRING];

	snprintf(buf, MAXPDSTRING, "%s", x->x_canvas->gl_name->s_name);
	x->x_patch_name = gensym(buf);

	snprintf(buf, MAXPDSTRING, "%s", canvas_getdir(x->x_canvas)->s_name);
	x->x_patch_path = gensym(buf);

	outlet_symbol(x->x_outlet_name, x->x_patch_name);
	outlet_symbol(x->x_outlet_path,x->x_patch_path);
}

/**
    Handles a integer (float??) input
*/
static void sensel_set(t_sensel *x, t_floatarg f)
{
	post("sensel got number %d", (int)f);
}

static void sensel_connect(t_sensel *x, t_floatarg f)
{
    if (x->x_connected == 1)
    {
        post("Sensel device already connected, please disconnect before trying to connect again.");
        return;
    }
    //List of all available Sensel devices
    SenselDeviceList list;

    //Firmware info from the Sensel device
    SenselFirmwareInfo fw_info; // move this to the struct?

    //Get a list of avaialble Sensel devices
    senselGetDeviceList(&list);
    if (list.num_devices == 0)
    {
        post("No Sensel device found");
        //return;
    }

    //Open a Sensel device by the id in the SenselDeviceList, handle initialized 
    senselOpenDeviceByID(&x->x_handle, list.devices[0].idx);
    //Get the firmware info
    senselGetFirmwareInfo(x->x_handle, &fw_info);
    //Get the sensor info
    senselGetSensorInfo(x->x_handle, &x->x_sensor_info);

    post("Sensel Device: %s" ,list.devices[0].serial_num);
    post("Firmware Version: %d.%d.%d", fw_info.fw_version_major, fw_info.fw_version_minor, fw_info.fw_version_build);
    post("Width: %fmm", x->x_sensor_info.width);
    post("Height: %fmm", x->x_sensor_info.height);
    post("Cols: %d", x->x_sensor_info.num_cols);
    post("Rows: %d", x->x_sensor_info.num_rows);

    //Set the frame content to scan contact data
    senselSetFrameContent(x->x_handle, FRAME_CONTENT_CONTACTS_MASK);
    //Allocate a frame of data, must be done before reading frame data
    senselAllocateFrameData(x->x_handle, &x->x_frame);
    //Start scanning the Sensel device
    senselStartScanning(x->x_handle);
    
    x->x_connected = 1;
}

/**
    Handles a key press
*/
static void sensel_float(t_sensel *x, t_floatarg fl) 
{
    if (x->x_connected == 1)
    {

        if (fl == 49) //1 
        {
            unsigned int num_frames = 0;
            //Read all available data from the Sensel device
            senselReadSensor(x->x_handle); // <<< FAIL
            //Get number of frames available in the data read from the sensor
            senselGetNumAvailableFrames(x->x_handle, &num_frames);

            senselGetSensorInfo(x->x_handle, &x->x_sensor_info);
            post("Width: %fmm NFrames: %d", x->x_sensor_info.width, num_frames);

            //t_floatarg total_force = 0;

            for (int f = 0; f < num_frames; f++)
            {
                //Read one frame of data
                senselGetFrame(x->x_handle, x->x_frame);
                if (x->x_frame->n_contacts > 0)
                {
                    post("Num Contacts: %d", x->x_frame->n_contacts);
                }
                //Calculate the total force
                /*total_force = 0;
                for (int i = 0; i < sensor_info.num_cols*sensor_info.num_rows; i++)
                {
                    total_force = total_force + x->x_frame->force_array[i];
                }
                post("Total Force : %f", total_force);*/
            }
        }
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
	t_sensel *x = (t_sensel *)pd_new(sensel_class);
	x->x_atom = *argv;
	t_glist *glist=(t_glist *)canvas_getcurrent(); 
	x->x_canvas=(t_canvas *)glist_getcanvas(glist);

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
    
	x->x_outlet_path = outlet_new(&x->x_obj, &s_symbol);
	x->x_outlet_name = outlet_new(&x->x_obj, &s_symbol);

    x->x_connected = 0;

	// don't touch stuff above

	
	// don't touch stuff below

	return(x);
}

/**
    Also.... a constructor....?
*/
void sensel_setup(void)
{
	sensel_class = class_new(gensym("sensel"), 
		(t_newmethod)sensel_new, NULL, 
		sizeof(t_sensel), 0, A_GIMME, 0);

	class_addbang(sensel_class, (t_method)sensel_bang);
	class_addmethod(sensel_class, (t_method)sensel_set,
		gensym("set"), A_FLOAT, 0);
    class_addmethod(sensel_class, (t_method)sensel_float,
        gensym("float"), A_FLOAT, 0);
    class_addmethod(sensel_class, (t_method)sensel_connect,
        gensym("connect"), A_FLOAT, 0);
}
