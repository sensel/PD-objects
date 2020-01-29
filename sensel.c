#include <stdio.h>
#include <string.h>
#include <m_pd.h>
#include "g_canvas.h"

#include <unistd.h>
#include <stdlib.h>
#include "src/sensel.h"
#include "src/sensel_device.h"

#define DEBUG(x)

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

    int num_devices;
    SENSEL_HANDLE x_handle[SENSEL_MAX_DEVICES];
    SenselSensorInfo x_sensor_info[SENSEL_MAX_DEVICES];
    SenselFrameData *x_frame;
    int x_connected;

} t_sensel;

// forward declarations
void sensel_poll(t_sensel *x, int which);

/**
    Handles a bang input
*/
static void sensel_bang(t_sensel *x)
{
    for (int i = 0; i < x->num_devices; i++) {
        sensel_poll(x, i);
    }
}

/**
    Handles a integer input
*/
static void sensel_set(t_sensel *x, t_floatarg f)
{
	post("sensel got number %d", (int)f);
}

void sensel_connect(t_sensel *x, t_floatarg f)
{
    if (x->x_connected == 1)
    {
        post("Sensel device already connected, please disconnect before trying to connect again.");
        return;
    }
    //List of all available Sensel devices
    SenselDeviceList list;

    //Firmware info from the Sensel device
    //SenselFirmwareInfo fw_info;

    //Get a list of available Sensel devices
    senselGetDeviceList(&list);
    if (list.num_devices == 0)
    {
        post("No Sensel device found");
        return;
    }

    //Open all Sensel devices by the id in the SenselDeviceList, handle initialized 
    for (int i = 0; i < list.num_devices; i++) {
        x->num_devices = list.num_devices;
        senselOpenDeviceByID(&x->x_handle[i], list.devices[i].idx);
        //Get the sensor info
        senselGetSensorInfo(x->x_handle[i], &x->x_sensor_info[i]);
        //Set the frame content to scan contact data
        senselSetFrameContent(x->x_handle[i], FRAME_CONTENT_CONTACTS_MASK);
        //Allocate a frame of data, must be done before reading frame data
        senselAllocateFrameData(x->x_handle[i], &x->x_frame);
        post("Sensel Device: %s", list.devices[i].serial_num);
        //post("Firmware Version: %d.%d.%d", fw_info.fw_version_major, fw_info.fw_version_minor, fw_info.fw_version_build);
        post("Width: %fmm", x->x_sensor_info[i].width);
        post("Height: %fmm", x->x_sensor_info[i].height);
        post("Cols: %d", x->x_sensor_info[i].num_cols);
        post("Rows: %d", x->x_sensor_info[i].num_rows);
    }

    /**
    //Open a Sensel device by the id in the SenselDeviceList, handle initialized 
    senselOpenDeviceByID(&x->x_handle[0], list.devices[0].idx);
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
    */
    
    x->x_connected = 1;
}

void sensel_poll(t_sensel *x, int which) {
    //arg "which" refers to which device in the handle array
    //is being polled

    if (x->x_connected == 1) {

        unsigned int num_frames = 0;

        //Start scanning the Sensel device
        senselStartScanning(x->x_handle[which]);

        //Read all available data from the Sensel device
        senselReadSensor(x->x_handle[which]);

        senselGetNumAvailableFrames(x->x_handle[which], &num_frames);

        senselGetSensorInfo(x->x_handle[which], &x->x_sensor_info[which]);

        

        for (int f = 0; f < num_frames; f++)
        {

            //Read one frame of data
            senselGetFrame(x->x_handle[which], x->x_frame);

            t_atom* ap = (t_atom*)getbytes(x->x_frame->n_contacts*18*sizeof(t_atom));

            for (int c = 0; c < x->x_frame->n_contacts; c++) {
                
                //Allow all contents to be seen
                senselSetContactsMask(x->x_handle[which], 0x0F);

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

                SETFLOAT(&orientation, x->x_frame->contacts[c].orientation);
                SETFLOAT(&major_axis,  x->x_frame->contacts[c].major_axis);
                SETFLOAT(&minor_axis,  x->x_frame->contacts[c].minor_axis);
                SETFLOAT(&delta_x,     x->x_frame->contacts[c].delta_x);
                SETFLOAT(&delta_y,     x->x_frame->contacts[c].delta_y);
                SETFLOAT(&delta_force, x->x_frame->contacts[c].delta_force);
                SETFLOAT(&delta_area,  x->x_frame->contacts[c].delta_area);
                SETFLOAT(&min_x,       x->x_frame->contacts[c].min_x);
                SETFLOAT(&min_y,       x->x_frame->contacts[c].min_y);
                SETFLOAT(&max_x,       x->x_frame->contacts[c].max_x);
                SETFLOAT(&max_y,       x->x_frame->contacts[c].max_y);
                SETFLOAT(&peak_x,      x->x_frame->contacts[c].peak_x);
                SETFLOAT(&peak_y,      x->x_frame->contacts[c].peak_y);
                SETFLOAT(&peak_force,  x->x_frame->contacts[c].peak_force);
                SETFLOAT(&x_pos,       x->x_frame->contacts[c].x_pos);
                SETFLOAT(&y_pos,       x->x_frame->contacts[c].y_pos);
                SETFLOAT(&total_force, x->x_frame->contacts[c].total_force);
                SETFLOAT(&area,        x->x_frame->contacts[c].area);

                ap[(c * 18) + 0]  = orientation;
                ap[(c * 18) + 1]  = major_axis;
                ap[(c * 18) + 2]  = minor_axis;
                ap[(c * 18) + 3]  = delta_x;
                ap[(c * 18) + 4]  = delta_y;
                ap[(c * 18) + 5]  = delta_force;
                ap[(c * 18) + 6]  = delta_area;
                ap[(c * 18) + 7]  = min_x;
                ap[(c * 18) + 8]  = min_y;
                ap[(c * 18) + 9]  = max_x;
                ap[(c * 18) + 10] = max_y;
                ap[(c * 18) + 11] = peak_x;
                ap[(c * 18) + 12] = peak_y;
                ap[(c * 18) + 13] = peak_force;
                ap[(c * 18) + 14] = x_pos;
                ap[(c * 18) + 15] = y_pos;
                ap[(c * 18) + 16] = total_force;
                ap[(c * 18) + 17] = area;
            }

            outlet_list(x->x_outlet, gensym("list"), 18*x->x_frame->n_contacts, ap);
        }

        senselStopScanning(x->x_handle[which]);
    }
    else {
        post("You must connect your Sensel first, using a 'connect' message!");
    }
}

void sensel_float(t_sensel *x, t_floatarg fl) 
{
    post("sensel got number %f", fl); //is this the right format lol
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
    //

	x->x_outlet = outlet_new(&x->x_obj, &s_list);

    x->x_connected = 0;
    x->x_frame = NULL;
    //x->x_handle = NULL;

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
	class_addmethod(sensel_class, (t_method)sensel_set,
		gensym("set"), A_FLOAT, 0);
    class_addmethod(sensel_class, (t_method)sensel_float,
        gensym("float"), A_FLOAT, 0);
    class_addmethod(sensel_class, (t_method)sensel_connect,
        gensym("connect"), A_FLOAT, 0);
    class_addmethod(sensel_class, (t_method)sensel_poll,
        gensym("poll"), A_FLOAT, 0);
}
