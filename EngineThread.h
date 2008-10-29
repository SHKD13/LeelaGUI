#ifndef ENGINETHREAD_H
#define ENGINETHREAD_H

#include "stdafx.h"
#include "GameState.h"

class MainFrame;

class TEngineThread : public wxThread {
    public:
        TEngineThread(GameState * gamestate, wxSemaphore * sema, MainFrame * frame);
        void limit_visits(int visits);
        void set_resigning(bool res);
        void set_analyzing(bool flag);
        void set_nopass(bool flag);
        void set_quiet(bool flag);
        void stop_engine(void);
        virtual void * Entry();
    private:
        GameState * m_state; 
        wxSemaphore * m_sema;
        MainFrame * m_frame;                 
        int m_maxvisits;  
        bool m_resigning;  
        bool m_analyseflag;  	
        bool m_quiet;
        bool m_nopass;
        bool m_runflag;    
};

#endif