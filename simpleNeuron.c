/**
	simpleNeuron - a max object that models a neuron
	Phillip Hermans, Dartmouth College, Fall 2012
	from jeremy bernstein's simplemax example	

*/

#include "ext.h"							// standard Max include, always required
#include "ext_obex.h"						// required for new style Max object

////////////////////////// object struct
typedef struct _simpleNeuron 
{
	t_object	ob;		// the object itself (must be first)
	
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

///////////////////////// function prototypes
//// standard set
void *simpleNeuron_new(t_symbol *s, long argc, t_atom *argv);
void simpleNeuron_free(t_simpleNeuron *x);
void simpleNeuron_assist(t_simpleNeuron *x, void *b, long m, long a, char *s);

// simple shit
void simpleBang(t_simpleNeuron *s);
void simpleFloat(t_simpleNeuron *s, double input);

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


//////////////////////// global class pointer variable
void *simpleNeuron_class;

int main(void)
{	
	
	// object initialization
	t_class *c;
	
	c = class_new("simpleNeuron", (method)simpleNeuron_new, (method)simpleNeuron_free, (long)sizeof(t_simpleNeuron), 
				  0L /* leave NULL!! */, A_GIMME, 0);
	
	/* you CAN'T call this from the patcher */
    class_addmethod(c, (method)simpleNeuron_assist,			"assist",		A_CANT, 0);  
	class_addmethod(c, (method)simpleBang, "bang", 0);
	class_addmethod(c, (method)simpleFloat, "float", A_FLOAT, 0);
	class_addmethod(c, (method)setMode, "mode", A_GIMME, 0);
	class_addmethod(c, (method)setThresh, "Vth", A_GIMME, 0);
	class_addmethod(c, (method)setR, "R", A_GIMME, 0);
	class_addmethod(c, (method)setC, "C", A_GIMME, 0);
	class_addmethod(c, (method)setStep, "step", A_GIMME, 0);
	
	class_register(CLASS_BOX, c); /* CLASS_NOBOX */
	simpleNeuron_class = c;

	post("I am the simpleNeuron object");
	return 0;
}

void simpleNeuron_assist(t_simpleNeuron *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) { // inlet
		sprintf(s, "I am inlet %ld", a);
	} 
	else {	// outlet
		sprintf(s, "I am outlet %ld", a); 			
	}
}

void simpleNeuron_free(t_simpleNeuron *x)
{
	object_free(x->m_clock1);
	object_free(x->m_clock2);
}

void simpleBang(t_simpleNeuron *s) // display stuff
{
	if (s->l_mode < 2)
		post("V is: %.2f", s->d_V);
	else 
		post("V is: %.2f", s->d_W);

	post("R is: %.2f", s->d_R);
	post("C is: %.2f", s->d_C);
	post("Mode is: %ld", s->l_mode);
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

void simpleFloat(t_simpleNeuron *s, double input) // input current
{
	s->d_I = input;
	int i;
	
	if (s->l_ref == 0)
	{
		if (s->l_mode == 0)
			s->d_V = s->d_V + s->d_I/s->d_C;
		else if (s->l_mode == 1)
		{
			s->d_V = s->d_V + s->d_I/s->d_C - s->d_V/(s->d_R*s->d_C);
			clock_fdelay(s->m_clock2, s->d_leakPer);
		}
		else if (s->l_mode == 2)
		{
			for (i = 0; i < 20; i++)
			{
				s->d_V = s->d_V + s->d_stepSize*(s->d_V - (s->d_V*s->d_V*s->d_V)/3 - s->d_W + s->d_I);
				s->d_W = s->d_W + s->d_stepSize*(s->d_V - s->d_a - s->d_b*s->d_W)/s->d_tao; // calculate dW/dt
				if (s->d_V > 1.9 && s->l_bangFlag == 0)
					{
						clock_fdelay(s->m_clock3, s->d_bangD);
						s->l_bangFlag = 1;
					}
			}
		}
		
		if (s->l_mode == 2)
		{
			if (s->d_V > 1.9 && s->l_bangFlag == 0)
			{
				clock_fdelay(s->m_clock3, s->d_bangD);
				s->l_bangFlag = 1;
			} else if (s->d_W < 1 && s->l_bangFlag == 1)
				s->l_bangFlag = 0;
		}
		else if (s->d_V > s->d_Vth)
			{
				clock_fdelay(s->m_clock3, s->d_bangD);
				s->l_ref = 1;
				clock_fdelay(s->m_clock1, s->d_absRef);
			}
	}
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
	if (s->d_V > 0 && s->d_V < s->d_Vth)
	{
		s->d_V = s->d_V - s->d_V/(s->d_R*s->d_C);
		//post("leak");
	}
	
	if(s->d_V > 0)
		clock_fdelay(s->m_clock2, s->d_leakPer);
	else
		s->d_V = 0;
}

void delayedBang(t_simpleNeuron *s)
{
	//post("BANG!");
	outlet_bang(s->m_outlet1);
}

void setMode(t_simpleNeuron *s, t_symbol *sym, long argc, t_atom *argv)
{
	s->l_mode = atom_getlong(argv);
	post("Mode set to: %ld", s->l_mode);
	// zero everything out
	s->d_V = 0;
	s->d_W = 0;
}



void *simpleNeuron_new(t_symbol *s, long argc, t_atom *argv)
{
	t_simpleNeuron *x = NULL;
    long i;

	// object instantiation
	if (x = (t_simpleNeuron *)object_alloc(simpleNeuron_class)) {
        object_post((t_object *)x, "a new %s object was instantiated: 0x%X", s->s_name, x);
        object_post((t_object *)x, "it has %ld arguments", argc);
		x = (t_simpleNeuron *)object_alloc(simpleNeuron_class);
		x->m_outlet1 = bangout((t_simpleNeuron *)x);
		x->m_clock1 = clock_new((t_simpleNeuron *)x, (method)delayedReset);
		x->m_clock2 = clock_new((t_simpleNeuron *)x, (method)leak);
		x->m_clock3 = clock_new((t_simpleNeuron *)x, (method)delayedBang);
        
        for (i = 0; i < argc; i++) {
            if ((argv + i)->a_type == A_LONG) {
                object_post((t_object *)x, "arg %ld: long (%ld)", i, atom_getlong(argv+i));
            } else if ((argv + i)->a_type == A_FLOAT) {
                object_post((t_object *)x, "arg %ld: float (%f)", i, atom_getfloat(argv+i));
            } else if ((argv + i)->a_type == A_SYM) {
                object_post((t_object *)x, "arg %ld: symbol (%s)", i, atom_getsym(argv+i)->s_name);
            } else {
                object_error((t_object *)x, "forbidden argument");
            }
        }
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
	
	x->l_mode = 0;
	x->l_ref = 0;
	
	x->d_absRef = 100;
	x->d_leakPer = 100;
	x->d_bangD = 10;
	
	return (x);
}

