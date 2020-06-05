struct sigaction
{
   void* sa_handler;
};

void sigaction(int signal, struct sigaction * sa, void* p) {}