/**
	simpleNeuron~: a simple biological neuron model with audio in/out
	Phillip Hermans, March 2015
 */

#include "ext.h"			// standard Max include, always required (except in Jitter)
#include "ext_obex.h"		// required for "new" style objects
#include "z_dsp.h"			// required for MSP objects


// struct to represent the object's state
typedef struct _simpleNeuron {
    t_pxobject		ob;			// the object itself (t_pxobject in MSP instead of t_object)
    
    void		*m_outlet1; // outputs a bang when fired
    void		*m_clock1;  // delayed reset clock
    void		*m_clock2;  // leaking clock
    void		*m_clock3;  // delay bang
    
    long		m_in;	// inlet number used by proxies
    long		l_ref;  // is it refractory?
    long		l_mode;	// which neural model to use
    long		l_bangFlag; // flag for when it just banged
    
    
    double		d_bangD;  // bang delay in miliseconds
    double		d_leakPer; // period of leaking in miliseconds
    double		d_absRef; // refractory period in miliseconds
    double		d_R;	// resistance
    double		d_C;	// capacitance
    double		d_I;	// input current
    double		d_V;	// voltage
    // shit for Fitzhug-Naguro
    double		d_W;	// a "recovery" variable
    double		d_Wth;  // recover threshold
    double		d_Wr;	// recover reset thresh
    double      d_Vth;  // voltage threshold
    double		d_a;
    double		d_b;
    double		d_tao;
    double		d_stepSize;
} t_simpleNeuron;


// method prototypes
void *simpleNeuron_new(t_symbol *s, long argc, t_atom *argv);
void simpleNeuron_free(t_simpleNeuron *x);
void simpleNeuron_assist(t_simpleNeuron *x, void *b, long m, long a, char *s);

void simpleNeuron_float(t_simpleNeuron *x, double f);
void simpleNeuron_dsp(t_simpleNeuron *x, t_signal **sp, short *count);
void simpleNeuron_dsp64(t_simpleNeuron *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
t_int *simpleNeuron_perform(t_int *w);
void simpleNeuron_perform64(t_simpleNeuron *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam);

// main neural function
void perform(t_simpleNeuron *s, double input);

// clock methods
void delayedReset(t_simpleNeuron *s);
void delayedBang(t_simpleNeuron *s);
void leak(t_simpleNeuron *s);

// set modes
void setMode(t_simpleNeuron *s, t_symbol *sym, long argc, t_atom *argv);
void setR(t_simpleNeuron *s, t_symbol *sym, long argc, t_atom *argv);
void setC(t_simpleNeuron *s, t_symbol *sym, long argc, t_atom *argv);
void setThresh(t_simpleNeuron *s, t_symbol *sym, long argc, t_atom *argv);
void setStep(t_simpleNeuron *s, t_symbol *sym, long argc, t_atom *argv);


// global class pointer variable
static t_class *simpleNeuron_class = NULL;


//***********************************************************************************************

int C74_EXPORT main(void)
{
    // object initialization, note the use of dsp_free for the freemethod, which is required
    // unless you need to free allocated memory, in which case you should call dsp_free from
    // your custom free function.
    
    t_class *c = class_new("simpleNeuron~", (method)simpleNeuron_new, (method)dsp_free, (long)sizeof(t_simpleNeuron), 0L, A_GIMME, 0);
    
    class_addmethod(c, (method)simpleNeuron_float,		"float",	A_FLOAT, 0);
    class_addmethod(c, (method)simpleNeuron_dsp,		"dsp",		A_CANT, 0);		// Old 32-bit MSP dsp chain compilation for Max 5 and earlier
    class_addmethod(c, (method)simpleNeuron_dsp64,		"dsp64",	A_CANT, 0);		// New 64-bit MSP dsp chain compilation for Max 6
    class_addmethod(c, (method)simpleNeuron_assist,	"assist",	A_CANT, 0);
    
    class_addmethod(c, (method)setMode, "mode", A_GIMME, 0);
    class_addmethod(c, (method)setThresh, "Vth", A_GIMME, 0);
    class_addmethod(c, (method)setR, "R", A_GIMME, 0);
    class_addmethod(c, (method)setC, "C", A_GIMME, 0);
    class_addmethod(c, (method)setStep, "step", A_GIMME, 0);
    
    class_dspinit(c);
    class_register(CLASS_BOX, c);
    simpleNeuron_class = c;
    
    return 0;
}


void *simpleNeuron_new(t_symbol *s, long argc, t_atom *argv)
{
    t_simpleNeuron *x = (t_simpleNeuron *)object_alloc(simpleNeuron_class);
    
    if (x) {
        post("simpleNeuron~ added");
        dsp_setup((t_pxobject *)x, 1);	// MSP inlets: arg is # of inlets and is REQUIRED!
        // use 0 if you don't need inlets
        x->m_outlet1 = bangout((t_simpleNeuron *)x);
        x->m_clock1 = clock_new((t_simpleNeuron *)x, (method)delayedReset);
        x->m_clock2 = clock_new((t_simpleNeuron *)x, (method)leak);
        x->m_clock3 = clock_new((t_simpleNeuron *)x, (method)delayedBang);
        
        outlet_new(x, "signal"); 		// signal outlet (note "signal" rather than NULL)
    }
    
    // default settings
    x->d_V = 0;
    x->d_Vth = 5;
    x->d_C = 1;
    x->d_R = 100;
    // Fitzhugh
    x->d_W = 0;
    x->d_Wth = 1.5;
    x->d_Wr  = 1.3;
    x->d_a = -0.7;  // http://www.scholarpedia.org/article/FitzHugh-Nagumo_model
    x->d_b = 0.8;
    x->d_tao = 12.5;
    x->d_stepSize = 0.3; // this needs to be
    x->l_bangFlag = 0;
    
    x->l_mode = 0; // which neuron mode: 0 IF, 1 LIF, 2 Fitzhugh
    x->l_ref = 0;
    
    x->d_absRef = 100;
    x->d_leakPer = 100;
    x->d_bangD = 10;
    return (x);
}


// NOT CALLED!, we use dsp_free for a generic free function
void simpleNeuron_free(t_simpleNeuron *x)
{
    ;
}


//***********************************************************************************************

void simpleNeuron_assist(t_simpleNeuron *x, void *b, long m, long a, char *s)
{
    if (m == ASSIST_INLET) { //inlet
        sprintf(s, "I am inlet %ld", a);
    }
    else {	// outlet
        sprintf(s, "I am outlet %ld", a);
    }
}


void simpleNeuron_float(t_simpleNeuron *x, double f)
{
}


//***********************************************************************************************

// this function is called when the DAC is enabled, and "registers" a function for the signal chain in Max 5 and earlier.
// In this case we register the 32-bit, "simplemsp_perform" method.
void simpleNeuron_dsp(t_simpleNeuron *x, t_signal **sp, short *count)
{
    post("my sample rate is: %f", sp[0]->s_sr);
    
    // dsp_add
    // 1: (t_perfroutine p) perform method
    // 2: (long argc) number of args to your perform method
    // 3...: argc additional arguments, all must be sizeof(pointer) or long
    // these can be whatever, so you might want to include your object pointer in there
    // so that you have access to the info, if you need it.
    dsp_add(simpleNeuron_perform, 4, x, sp[0]->s_vec, sp[1]->s_vec, sp[0]->s_n);
}


// this is the Max 6 version of the dsp method -- it registers a function for the signal chain in Max 6,
// which operates on 64-bit audio signals.
void simpleNeuron_dsp64(t_simpleNeuron *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
    post("my sample rate is: %f", samplerate);
    
    // instead of calling dsp_add(), we send the "dsp_add64" message to the object representing the dsp chain
    // the arguments passed are:
    // 1: the dsp64 object passed-in by the calling function
    // 2: the symbol of the "dsp_add64" message we are sending
    // 3: a pointer to your object
    // 4: a pointer to your 64-bit perform method
    // 5: flags to alter how the signal chain handles your object -- just pass 0
    // 6: a generic pointer that you can use to pass any additional data to your perform method
    
    object_method(dsp64, gensym("dsp_add64"), x, simpleNeuron_perform64, 0, NULL);
}


//***********************************************************************************************

// this is the 32-bit perform method for Max 5 and earlier
t_int *simpleNeuron_perform(t_int *w)
{
    // DO NOT CALL post IN HERE, but you can call defer_low (not defer)
    
    // args are in a vector, sized as specified in simplemsp_dsp method
    // w[0] contains &simplemsp_perform, so we start at w[1]
    t_simpleNeuron *x = (t_simpleNeuron *)(w[1]);
    t_float *inL = (t_float *)(w[2]);
    t_float *outL = (t_float *)(w[3]);
    int n = (int)w[4];
    
    // this perform method simply copies the input to the output, offsetting the value
    while (n--)
        *outL++ = *inL++;
    
    // you have to return the NEXT pointer in the array OR MAX WILL CRASH
    return w + 5;
}


// this is 64-bit perform method for Max 6
void simpleNeuron_perform64(t_simpleNeuron *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam)
{
    t_double *inL = ins[0];		// we get audio for each inlet of the object from the **ins argument
    t_double *outL = outs[0];	// we get audio for each outlet of the object from the **outs argument
    int n = sampleframes;
    
    // this perform method simply copies the input to the output, offsetting the value
    while (n--)
    {
        if( x->d_V < x->d_Vth) // if under threshold
        {
            perform(x, *inL++);
        } else {
            leak(x);
        }
        
        *outL++ = x->d_V;
    }
}

void perform(t_simpleNeuron *s, double input) // input current
{
    s->d_I = input;
    
    if (s->l_ref == 0) // if not in refractory period
    {
        if (s->l_mode == 0) // regular
            s->d_V = s->d_V + s->d_I/s->d_C;
        else if (s->l_mode == 1) // leaky
        {
            s->d_V = s->d_V + s->d_I/s->d_C - s->d_V/(s->d_R*s->d_C);
            leak(s);
            //clock_fdelay(s->m_clock2, s->d_leakPer);
        }
        else if (s->l_mode == 2) //  fitzhugh
        {

            s->d_V = s->d_V + s->d_stepSize*(s->d_V - (s->d_V*s->d_V*s->d_V)/3 - s->d_W + s->d_I);
            s->d_W = s->d_W + s->d_stepSize*(s->d_V - s->d_a - s->d_b*s->d_W)/s->d_tao; // calculate dW/dt
            
            /*
            if (s->d_V > 1.9 && s->l_bangFlag == 0)
            {
                clock_fdelay(s->m_clock3, s->d_bangD);
                s->l_bangFlag = 1;
            }
            if (s->d_V > 1.9 && s->l_bangFlag == 0)
            {
                clock_fdelay(s->m_clock3, s->d_bangD);
                s->l_bangFlag = 1;
            } else if (s->d_W < 1 && s->l_bangFlag == 1)
             {
                s->l_bangFlag = 0;
             }
             */
        }
        else if (s->d_V > s->d_Vth) // if over threshold, go into refractory
        {
            //clock_fdelay(s->m_clock3, s->d_bangD);
            s->l_ref = 1;
            s->l_bangFlag = 1;
            delayedBang(s);
            //clock_fdelay(s->m_clock1, s->d_absRef);
        }
    }
}

void setR(t_simpleNeuron *s, t_symbol *sym, long argc, t_atom *argv) // set resistance
{
    s->d_R = atom_getfloat(argv);
    post("Resistance set to: %.2f", s->d_R);
}

void setC(t_simpleNeuron *s, t_symbol *sym, long argc, t_atom *argv) // set capacitance
{
    s->d_C = atom_getfloat(argv);
    post("Capacitance set to: %.2f", s->d_C);
}

void setThresh(t_simpleNeuron *s, t_symbol *sym, long argc, t_atom *argv)
{
    s->d_Vth = atom_getfloat(argv);
    post("Threshold set to: %.2f", s->d_Vth);
}

void setStep(t_simpleNeuron *s, t_symbol *sym, long argc, t_atom *argv)
{
    s->d_stepSize = atom_getfloat(argv);
    post("Step size set to: %.2f", s->d_stepSize);
}

void setMode(t_simpleNeuron *s, t_symbol *sym, long argc, t_atom *argv)
{
    s->l_mode = atom_getlong(argv);
    post("Mode set to: %ld", s->l_mode);
    // zero everything out
    s->d_V = 0;
    s->d_W = 0;
}

void delayedReset(t_simpleNeuron *s)
{
    s->l_ref = 0;
    s->d_V = 0;
    s->d_W = 0;
    //post("Reset");
}

void leak(t_simpleNeuron *s)
{
    if (s->d_V > 0 && s->d_V < s->d_Vth) // then leak
    {
        s->d_V = s->d_V - s->d_V/(s->d_R*s->d_C);
        s->l_bangFlag = 0;
        //post("leak");
    } else {
        
    }
    
    /*
    if(s->d_V > 0)
        clock_fdelay(s->m_clock2, s->d_leakPer);
    else
        s->d_V = 0;
     */
}

void delayedBang(t_simpleNeuron *s)
{
    //post("BANG!");
    outlet_bang(s->m_outlet1);
}
