#include <stdio.h>
#include <string.h>
#include <m_pd.h>
#include "g_canvas.h"

#include <unistd.h>
#include <stdlib.h>
#include "sensel.h"
#include "sensel_device.h"

#ifdef __MINGW32__
	#include <pthread.h>
#elif defined WIN32
	//TODO: include windows ifdef for threads
#else
	#include <pthread.h>
#endif

//#define DEBUG(x)

/**
  the Sensel Morph Pd external, written by
  Rachel Hachem <rachelly@vt.edu>
  under the guidance of Dr. Ivica Ico Bukvic <ico@vt.edu>

  v.0.9.0 2020-04-13
  Initial release by Rachel Hachem <rachelly@vt.edu>
  under the guidance of Dr. Ivica Ico Bukvic <ico@vt.edu>

  v.1.0.0 2020-05-20
  Threaded implementation with additional features,
  including, disconnect, free, identify, improved
  console output, versioning, updated help file, and
  bug-fixes to support multiple sensel morphs by
  Ivica Ico Bukvic <ico@vt.edu>
  
  Its functionalities are as follows:
  - When sent a bang, it polls the Sensel for data and 
    outputs it as a list, with each contact being a
    separate list.
  - When sent a "discover" message, it will look for the
    first available Sensel device and connect to it.
  - When sent a "connect" message with a serial number as
    the argument, it will find that Sensel device and
    connect to it.
  - When sent a "disconnect" message the object will
  	disonnect connected Sensel device.
  - When sent "identify" message the object outputs a
  	list of all available Sensel devices' serial numbers.
*/

static t_class *sensel_class;

/*
	Single-linked list for accumulating data output
*/
typedef struct _data
{
	t_atom args[19];
	struct _data *next;
} t_data;

/*
    An array keeping track of which devices are
    already connected up to maximum allowed by
    the sensel API
*/
static t_symbol sensel_connected_devices[SENSEL_MAX_DEVICES];

/*
    Adds a device to the list of connected devices,
    returning:
        0 for success
       -1 for failure (shouldn't happen as API should throw error first)
*/
static int add_connected_to_sensel_device_list(t_symbol *s)
{
    int i = 0;

    while (i < SENSEL_MAX_DEVICES)
    {
        if (sensel_connected_devices[i].s_name == NULL)
        {
            sensel_connected_devices[i] = *s;
            return(0);
        }
        else
            i++;
    }
    return(-1);
}

/*
    Removes device from the list of connected devices,
    returning:
        0 for success
       -1 for failure (shouldn't happen as API should throw error first)
*/
static int remove_connected_to_sensel_device_list(t_symbol *s)
{
    int i = 0;

    while (i < SENSEL_MAX_DEVICES)
    {
        if (sensel_connected_devices[i].s_name != NULL)
        {
            if (!strcmp(sensel_connected_devices[i].s_name, s->s_name))
            {
                sensel_connected_devices[i].s_name = NULL;
                return(0);
            }
        i++;
        }
    }
    return(-1);
}

/*
    Checks if a device we wish to connect is already
    on the list of connected devices, returns:
        1 for yes
        0 for no
*/ 
static int check_if_already_on_sensel_device_list(t_symbol *s)
{
    int i = 0;

    while (i < SENSEL_MAX_DEVICES)
    {
        if (sensel_connected_devices[i].s_name != NULL)
        {
            if (!strcmp(sensel_connected_devices[i].s_name, s->s_name))
                return(1);
        }
        i++;
    }
    return(0);
}

/*
	Main Sensel Morph data structure
*/
typedef struct _sensel
{
	t_object x_obj;
	t_atom x_atom;
	t_canvas *x_canvas;

	t_symbol *x_patch_name;
	t_symbol *x_patch_path;
	t_symbol *x_remote_name;

	t_outlet *x_outlet_data;
	t_outlet *x_outlet_status;

	SENSEL_HANDLE x_handle;
	SenselSensorInfo x_sensor_info;
	SenselFrameData *x_frame;
	int x_connected;

	pthread_t unsafe_t;
	pthread_mutex_t unsafe_mutex;
	pthread_cond_t unsafe_cond;
	int unsafe;
	t_data *x_data;
	t_data *x_data_end;

	t_clock *x_clock_output;

    t_symbol *x_serial;
    
} t_sensel;

/*
	Struct for communicating with the Sensel thread
*/
typedef struct
{
	t_sensel *s_inst;
} threadedFunctionParams;

/*
	Forward declarations
*/
static void sensel_poll(t_sensel *x);

/*
	Threaded function that reads from the Sensel
	without blocking the main audio thread
*/
static void *sensel_pthreadForAudioUnfriendlyOperations(void *ptr)
{
	threadedFunctionParams *rPars = (threadedFunctionParams*)ptr;
	t_sensel *x = rPars->s_inst;

	while(x->unsafe > -1) {
		pthread_mutex_lock(&x->unsafe_mutex);
		// inform the external when the thread is ready
		if (x->unsafe == 1)
			x->unsafe = 0;
		pthread_cond_wait(&x->unsafe_cond, &x->unsafe_mutex);

		sensel_poll(x);

		pthread_mutex_unlock(&x->unsafe_mutex);
	}
	pthread_exit(0);
}

/*
    Handles a bang input; polls the Sensel
*/
static void sensel_bang(t_sensel *x)
{
    //pthread_mutex_lock(&x->unsafe_mutex);
    pthread_cond_signal(&x->unsafe_cond);
    //pthread_mutex_unlock(&x->unsafe_mutex);
}

/*
	Outputs received data via clock delay that is triggered
	from the sub-thread
*/
static void sensel_output_data(t_sensel *x)
{
	while (x->x_data != NULL)
	{
		outlet_list(x->x_outlet_data, gensym("list"), 19, x->x_data->args);
		t_data *last = x->x_data;
		x->x_data = x->x_data->next;
		free(last);
	}
}

/*
  Connects the Pd patch to a specific Sensel device, using
  the serial number as an argument.
*/
static void sensel_connect(t_sensel *x, t_symbol *s)
{
	post("sensel: connecting to device with serial number %s...", s->s_name);

    if (x->x_connected == 1)
    {
        error("sensel: connect failed--device already connected.");
        return;
    }

    if (check_if_already_on_sensel_device_list(s))
    {
        error("sensel: connect failed--device is already connected to another sensel object.");
        return;
    }

    //List of all available Sensel devices
    SenselDeviceList list;

    //Get a list of available Sensel devices
    senselGetDeviceList(&list);
    if (list.num_devices == 0)
    {
        error("sensel: connect failed--no device found.");
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
        post("sensel: successfully connected to device with a serial number %s.", s->s_name);
        x->x_connected = 1;

        x->x_serial = s;
        add_connected_to_sensel_device_list(x->x_serial);

        //sensel_connected_devices[0] = *gensym("blah");
        outlet_float(x->x_outlet_status, x->x_connected);
    }

    if (x->x_connected != 1) {
    	error("sensel: connect failed--device with a serial number %s not found.", s->s_name);
    }
}

/*
  Discovers and connects to the first available Sensel Morph
*/
static void sensel_discover(t_sensel *x)
{

    if (x->x_connected == 1)
    {
        error("sensel: discover failed--device already connected.");
        return;
    }
    
    //List of all available Sensel devices
    SenselDeviceList list;

    //Get a list of available Sensel devices
    senselGetDeviceList(&list);
    if (list.num_devices == 0)
    {
        error("sensel: discover failed--no device found.");
        return;
    }

    int i = 0;
    int found = 0;

    while(i < list.num_devices)
    {
        if (!check_if_already_on_sensel_device_list(gensym(list.devices[i].serial_num)))
        {
            found = 1;
            break;
        }
        i++;
    }
    if (!found)
    {
        error("sensel: discover failed--all discoverable devices are already connected to another sensel object.");
        return;
    }
 
    //Open a Sensel device by the id in the SenselDeviceList, handle initialized 
    senselOpenDeviceByID(&x->x_handle, list.devices[i].idx);
     
    //Get the sensor info
    senselGetSensorInfo(x->x_handle, &x->x_sensor_info);
    
    //Set the frame content to scan contact data
    senselSetFrameContent(x->x_handle, FRAME_CONTENT_CONTACTS_MASK);
    
    //Allocate a frame of data, must be done before reading frame data
    senselAllocateFrameData(x->x_handle, &x->x_frame);
    
    //Post information to the Pd console
    post("sensel: successfully connected to device with a serial number %s.", list.devices[i].serial_num);

    x->x_serial = gensym(list.devices[i].serial_num);
    add_connected_to_sensel_device_list(x->x_serial);

    x->x_connected = 1;
    outlet_float(x->x_outlet_status, x->x_connected);
}

/*
  Lists serial numbers of all available Sensel Morphs
*/
static void sensel_identify(t_sensel *x)
{
    // disable polling while doing identifying
    // as this tends to misbehave on Macs when
    // it gets this request while outputting data
    pthread_mutex_lock(&x->unsafe_mutex);
    
    //List of all available Sensel devices
    SenselDeviceList list;

    //Get a list of available Sensel devices
    senselGetDeviceList(&list);
    if (list.num_devices == 0)
    {
        post("sensel: identify found no devices.");
        return;
    }

    post("sensel: identify found following devices:");

    int i;
    for (i = 0; i < list.num_devices; i++)
 	{
    	post("%d: %s", i+1, list.devices[i].serial_num);
 	}
    // reenable polling since we are now done
    pthread_mutex_unlock(&x->unsafe_mutex);
}

/*
  Polls for the Sensel contact data. Outputs a list for every
  current contact, each comprised of 19 data points, listed
  clearly in the code.
*/
static void sensel_poll(t_sensel *x) {

    if (x->x_connected == 1) {

        unsigned int num_frames = 0;

        t_data *temp;

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


                if (x->x_data == NULL) // this is the first time around
                {
                	x->x_data = (t_data *)getbytes(sizeof(t_data));
                	x->x_data->next = NULL;

                	x->x_data_end = x->x_data;
                }
                else
                {
                	temp = (t_data *)getbytes(sizeof(t_data));
                	temp->next = NULL;

                	x->x_data_end->next = temp;
                	x->x_data_end = temp;
                }

                SETFLOAT(&(x->x_data_end->args[0]), whichContact+1);
                SETFLOAT(&(x->x_data_end->args[1]), x->x_frame->contacts[c].orientation);
                SETFLOAT(&(x->x_data_end->args[2]), x->x_frame->contacts[c].major_axis);
                SETFLOAT(&(x->x_data_end->args[3]), x->x_frame->contacts[c].minor_axis);
                SETFLOAT(&(x->x_data_end->args[4]), x->x_frame->contacts[c].delta_x);
                SETFLOAT(&(x->x_data_end->args[5]), x->x_frame->contacts[c].delta_y);
                SETFLOAT(&(x->x_data_end->args[6]), x->x_frame->contacts[c].delta_force);
                SETFLOAT(&(x->x_data_end->args[7]), x->x_frame->contacts[c].delta_area);
                SETFLOAT(&(x->x_data_end->args[8]), x->x_frame->contacts[c].min_x);
                SETFLOAT(&(x->x_data_end->args[9]), x->x_frame->contacts[c].min_y);
                SETFLOAT(&(x->x_data_end->args[10]),x->x_frame->contacts[c].max_x);
                SETFLOAT(&(x->x_data_end->args[11]),x->x_frame->contacts[c].max_y);
                SETFLOAT(&(x->x_data_end->args[12]),x->x_frame->contacts[c].peak_x);
                SETFLOAT(&(x->x_data_end->args[13]),x->x_frame->contacts[c].peak_y);
                SETFLOAT(&(x->x_data_end->args[14]),x->x_frame->contacts[c].peak_force);
                SETFLOAT(&(x->x_data_end->args[15]),x->x_frame->contacts[c].x_pos);
                SETFLOAT(&(x->x_data_end->args[16]),x->x_frame->contacts[c].y_pos);
                SETFLOAT(&(x->x_data_end->args[17]),x->x_frame->contacts[c].total_force);
                SETFLOAT(&(x->x_data_end->args[18]),x->x_frame->contacts[c].area);
            }
        }
        clock_delay(x->x_clock_output, 0);

        senselStopScanning(x->x_handle);
    }
}

/*
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
	post("DISIS Sensel Morph v.1.0.0");

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

    x->x_outlet_data = outlet_new(&x->x_obj, &s_list);
    x->x_outlet_status = outlet_new(&x->x_obj, &s_float);

    x->x_connected = 0;
    x->x_frame = NULL;
    x->x_data =  NULL;
    x->x_data_end = NULL;

    x->x_clock_output = clock_new(x, (t_method)sensel_output_data);

	// prep the secondary thread init variable
	x->unsafe = 1;

    threadedFunctionParams rPars;
	rPars.s_inst = x;
	pthread_mutex_init(&x->unsafe_mutex, NULL);
	pthread_cond_init(&x->unsafe_cond, NULL);
	pthread_create( &x->unsafe_t, NULL, (void *) &sensel_pthreadForAudioUnfriendlyOperations, (void *) &rPars);

	// wait until other thread has properly intialized so that
	// rPars do not get destroyed before the thread has gotten its
	// pointer information
	while(x->unsafe == 1) {
		sched_yield();
	}

	return(x);
}

/*
	Disconnects the Sensel Morph
*/
static void sensel_disconnect(t_sensel *x)
{
	if (x->x_connected)
	{
		x->x_connected = 0;
		pthread_mutex_lock(&x->unsafe_mutex);
		pthread_cond_signal(&x->unsafe_cond);
		pthread_mutex_unlock(&x->unsafe_mutex);
		senselFreeFrameData(x->x_handle, x->x_frame);
		senselClose(x->x_handle);

        remove_connected_to_sensel_device_list(x->x_serial);
        x->x_serial = NULL;

		outlet_float(x->x_outlet_status, x->x_connected);
	}
	else
	{
		error("sensel: disconnect failed--no device connected.");
	}
}


/*
	Frees the object (when it is deleted)
*/
static void sensel_free(t_sensel * x)
{
	if (x->x_connected) {
		sensel_disconnect(x);
	}

	x->unsafe = -1;

	pthread_mutex_lock(&x->unsafe_mutex);
	pthread_cond_signal(&x->unsafe_cond);
	pthread_mutex_unlock(&x->unsafe_mutex);

	pthread_join(x->unsafe_t, NULL);
	pthread_mutex_destroy(&x->unsafe_mutex);

	clock_free(x->x_clock_output);

	// delete any leftover single-linked output data list
	t_data *last = NULL;
	while (x->x_data != NULL)
	{
		outlet_list(x->x_outlet_data, gensym("list"), 19, x->x_data->args);
		last = x->x_data;
		x->x_data = x->x_data->next;
		free(last);
	}
}

/*
    Init sensel object
*/
void sensel_setup(void)
{
	sensel_class = class_new(gensym("sensel"), 
		(t_newmethod)sensel_new, (t_method)sensel_free, 
		sizeof(t_sensel), 0, A_GIMME, 0);

	class_addbang(sensel_class, (t_method)sensel_bang);
	class_addmethod(sensel_class, (t_method)sensel_connect,
        gensym("connect"), A_SYMBOL, 0);
    class_addmethod(sensel_class, (t_method)sensel_discover,
        gensym("discover"), 0);
	class_addmethod(sensel_class, (t_method)sensel_disconnect,
        gensym("disconnect"), 0);
	class_addmethod(sensel_class, (t_method)sensel_identify,
        gensym("identify"), 0);
    class_addmethod(sensel_class, (t_method)sensel_poll,
        gensym("poll"), 0);
}
