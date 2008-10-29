#include "stdafx.h"
#include "GUI.h"
#include "MainFrame.h"
#include "TBoardPanel.h"
#include "Zobrist.h"
#include "Random.h"
#include "SGFTree.h"
#include "EngineThread.h"
#include "AboutDialog.h"
#include "NewGameDialog.h"
#include "NagDialog.h"
#include "CopyProtectionDialog.h"
#include "Utils.h"
#include "ClockAdjustDialog.h"

DEFINE_EVENT_TYPE(EVT_NEW_MOVE)
DEFINE_EVENT_TYPE(EVT_BOARD_UPDATE)
DEFINE_EVENT_TYPE(EVT_STATUS_UPDATE)

MainFrame::MainFrame(wxFrame *frame, const wxString& title)
          :TMainFrame(frame, wxID_ANY, title) {   
        
    delete wxLog::SetActiveTarget(NULL); 

    wxLog::SetTimestamp(NULL);            

    Connect(EVT_NEW_MOVE, wxCommandEventHandler(MainFrame::doNewMove));   
    Connect(EVT_BOARD_UPDATE, wxCommandEventHandler(MainFrame::doBoardUpdate)); 
    Connect(EVT_STATUS_UPDATE, wxCommandEventHandler(MainFrame::doStatusUpdate)); 
    
    std::auto_ptr<Random> rng(new Random(5489UL));
    Zobrist::init_zobrist(*rng);    
    
    // init game   
    m_playerColor = FastBoard::BLACK;
    m_State.init_game(9, 7.5f);
    m_State.set_timecontrol(30 * 60 * 100, 0, 0);
    m_visitLimit = 1000;
    m_ratedGame = true;
    m_panelBoard->setState(&m_State);
    m_panelBoard->setPlayerColor(m_playerColor);
    m_analyzing = false;
    m_pondering = false;
    m_disputing = false;
    m_ponderedOnce = true;
    
    m_passEnabled = wxConfig::Get()->Read(wxT("passEnabled"), 1);     
    m_soundEnabled = wxConfig::Get()->Read(wxT("soundEnabled"), 1); 
    m_resignEnabled = wxConfig::Get()->Read(wxT("resignEnabled"), 1); 
    m_ponderEnabled = wxConfig::Get()->Read(wxT("ponderEnabled"), 1); 
    
    m_menuSettings->FindItem(ID_PASSTOGGLE)->Check(m_passEnabled);
    m_menuSettings->FindItem(ID_SOUNDSWITCH)->Check(m_soundEnabled);
    m_menuSettings->FindItem(ID_RESIGNTOGGLE)->Check(m_resignEnabled);
    m_menuSettings->FindItem(ID_PONDERTOGGLE)->Check(m_ponderEnabled);    
    
    // set global message area
    Utils::setGUIQueue(this->GetEventHandler(), EVT_STATUS_UPDATE);
    // allow one engine running (engine finished signal)
    m_engineRunning.Post();    
    
#ifdef LITEVERSION 
    m_menuAnalyze->FindItem(ID_ANALYZE)->Enable(false);
    m_toolBar1->EnableTool(ID_ANALYZE, false);
#endif
    
    SetIcon(wxICON(aaaa));

    SetSize(450, 550);
    Center();
    
    wxCommandEvent evt;
    doNewRatedGame(evt);
}

MainFrame::~MainFrame() {
    stopEngine();
    
    if (m_engineRunning.WaitTimeout(2000) == wxSEMA_TIMEOUT) {
        m_engineThread->Kill();
    }

    delete wxLog::SetActiveTarget(new wxLogStderr(NULL));       
    m_panelBoard->setState(NULL); 
    
    Disconnect(EVT_NEW_MOVE, wxCommandEventHandler(MainFrame::doNewMove));
    Disconnect(EVT_BOARD_UPDATE, wxCommandEventHandler(MainFrame::doBoardUpdate));
    Disconnect(EVT_STATUS_UPDATE, wxCommandEventHandler(MainFrame::doStatusUpdate));
    
    Hide();
    
#ifdef LITEVERSION    
    NagDialog dialog(this);
    
    dialog.ShowModal();
#endif    
}

void MainFrame::doStatusUpdate(wxCommandEvent& event) {
    m_statusBar->SetStatusText(event.GetString(), 1);
}

void MainFrame::SetStatusBar(wxString mess, int pos) {
    m_statusBar->SetStatusText(mess, pos);
}

void MainFrame::updateStatusBar(char *str) {
    //wxString wxstr(str);
    //SetStatusText(str);
}

// do whatever we need to do if the visible board gets updated
void MainFrame::doBoardUpdate(wxCommandEvent& event) {       
    wxString mess;
    mess.Printf(_("Komi: %d.5; Prisoners white: %d/black: %d"), 
                (int)m_State.get_komi(),
                m_State.board.get_prisoners(FastBoard::BLACK),
                m_State.board.get_prisoners(FastBoard::WHITE)
                );
    m_statusBar->SetStatusText(mess, 0);     
    Refresh();
}

void MainFrame::doExit(wxCommandEvent & event) {
    Close();
}

void MainFrame::doSoundToggle(wxCommandEvent& event) {
    m_soundEnabled = !m_soundEnabled;
    wxConfig::Get()->Write(wxT("soundEnabled"), m_soundEnabled);  
}

void MainFrame::startEngine() {
    if (m_engineRunning.TryWait() != wxSEMA_BUSY) { 
        if (!m_pondering) {
            m_engineThread = new TEngineThread(&m_State, &m_engineRunning, this);
        } else {
            m_ponderState = m_State;
            m_engineThread = new TEngineThread(&m_ponderState, &m_engineRunning, this);
        }
        if (m_engineThread->Create(1024 * 1024) != wxTHREAD_NO_ERROR) {
            ::wxLogDebug("Error starting engine");
        } else {            
            // lock the board
            if (!m_pondering) {
                m_panelBoard->lockState();
            }

            m_engineThread->limit_visits(m_visitLimit);
            m_engineThread->set_resigning(m_resignEnabled);
            m_engineThread->set_analyzing(m_analyzing | m_pondering);
            m_engineThread->set_quiet(!m_analyzing);
            if (m_passEnabled) {
                m_engineThread->set_nopass(m_disputing);
            } else {
                m_engineThread->set_nopass(true);
            }            
            m_engineThread->Run();
        }
    } else {
        ::wxLogDebug("Engine already running");
    }                
}

void MainFrame::stopEngine() {
    if (m_engineRunning.TryWait() == wxSEMA_BUSY) {
        m_engineThread->stop_engine();        
    } else {
        // trywait success, repost
        m_engineRunning.Post();
    }
}

void MainFrame::doToggleTerritory(wxCommandEvent& event) {
    m_panelBoard->setShowInfluence(!m_panelBoard->getShowInfluence());
    
    if (m_panelBoard->getShowInfluence()) {
        m_panelBoard->setShowMoyo(false);
        wxMenuItem * moyo = m_menuSettings->FindItem(ID_SHOWMOYO);
        moyo->Check(false);
    }
    
    m_panelBoard->setShowTerritory(false);
    
    m_panelBoard->Refresh();
}

void MainFrame::doToggleMoyo(wxCommandEvent& event) {
    m_panelBoard->setShowMoyo(!m_panelBoard->getShowMoyo());
    
    if (m_panelBoard->getShowMoyo()) {
        m_panelBoard->setShowInfluence(false);
        wxMenuItem * influence = m_menuSettings->FindItem(ID_SHOWTERRITORY);
        influence->Check(false);
    }
    
    m_panelBoard->setShowTerritory(false);
    
    m_panelBoard->Refresh();
}
	
void MainFrame::doNewMove(wxCommandEvent & event) {
    ::wxLogDebug(_("New move arrived"));
    
    m_panelBoard->unlockState();
   
    if (m_analyzing) {
        m_analyzing = false;
        m_ponderedOnce = true;
        return;
    }

    if (m_pondering) {
        stopEngine();
        m_pondering = false;
        m_ponderedOnce = true;
    } else {
        m_ponderedOnce = false;
    }
    
    if (m_State.get_last_move() != FastBoard::PASS) {
        if (m_soundEnabled) {
            wxSound tock("IDW_TOCK", true);
            tock.Play(wxSOUND_ASYNC);                              
        }
    } else {
        if (m_State.get_to_move() == m_playerColor 
            && m_State.get_last_move() == FastBoard::PASS) {
            
            ::wxMessageBox(_("Computer passes"), _("Pass"), wxOK, this);
        }
    }
    
    if (m_State.get_passes() >= 2 || m_State.get_last_move() == FastBoard::RESIGN) {                        
        float komi, score, prekomi, handicap;
        bool won;    
        scoreGame(won, komi, handicap, score, prekomi);
        bool accepts = scoreDialog(komi, score, prekomi, handicap);       
        if (accepts || m_State.get_last_move() == FastBoard::RESIGN) {
            ratedGameEnd(won);        
        } else {
            m_disputing = true;
            // undo passes
            m_State.undo_move();
            m_State.undo_move();
            if (m_State.get_to_move() != m_playerColor) {
                ::wxLogDebug("Computer to move"); 
                startEngine();                
            } else {
                m_pondering = true;
                startEngine();
            }
        }
    } else {
        if (m_State.get_to_move() != m_playerColor) {
            ::wxLogDebug("Computer to move"); 
            startEngine();                
        } else {
            if (m_ponderEnabled && !m_ratedGame && !m_analyzing && !m_ponderedOnce) {
                m_pondering = true;
                startEngine();
            }
        }
        m_panelBoard->setShowTerritory(false);
    }	
	
    // signal update of visible board
    wxCommandEvent myevent(EVT_BOARD_UPDATE, GetId());
    myevent.SetEventObject(this);                        
    ::wxPostEvent(m_panelBoard->GetEventHandler(), myevent);     
}

void MainFrame::doPaint(wxPaintEvent& event) {    
    //wxPaintDC tmp(this);        
    event.Skip();           
}

void MainFrame::doActivate(wxActivateEvent& event) {                       
    event.Skip();
}

void MainFrame::doResize(wxSizeEvent& event) { 
    event.Skip();    
}

void MainFrame::doBoardResize(wxSizeEvent& event) {
    event.Skip();
}

void MainFrame::doNewGame(wxCommandEvent& event) {
    stopEngine();
    m_engineRunning.Wait();
    
    NewGameDialog mydialog(this);
    
    if (mydialog.ShowModal() == wxID_OK) {
        ::wxLogDebug("OK clicked"); 
        
        m_State.init_game(mydialog.getBoardsize(), mydialog.getKomi());
        m_State.set_fixed_handicap(mydialog.getHandicap());        
        // max 60 minutes per game    
        m_State.set_timecontrol(mydialog.getTimeControl() * 30 * 100, 0, 0);
        m_visitLimit = mydialog.getSimulations();
        m_playerColor = mydialog.getPlayerColor();       
        m_panelBoard->setPlayerColor(m_playerColor);
        m_panelBoard->setShowTerritory(false);
        m_analyzing = false;
	m_pondering = false;
        m_disputing = false;
        m_ratedGame = false;
        
        m_engineRunning.Post();               
        
        wxCommandEvent myevent(EVT_NEW_MOVE, GetId());
        myevent.SetEventObject(this);                        
        ::wxPostEvent(m_panelBoard->GetEventHandler(), myevent);     
    } else {
        m_engineRunning.Post();
    }                     
}

void MainFrame::doNewRatedGame(wxCommandEvent& event) {    
    stopEngine();
    m_engineRunning.Wait();
    
    m_analyzing = false;   
    m_disputing = false;    
    m_pondering = false;
    
    int rank = wxConfig::Get()->Read(wxT("LastRank"), (long)-30);
    
    ::wxLogDebug("Last rank was: %d", rank); 
    
    wxString mess = wxString(_("Rank: "));
    mess += rankToString(rank);    
    m_statusBar->SetStatusText(mess, 1);
    
    this->SetTitle(_("Leela - ") + mess); 
    
    int handicap;
    int simulations;
    
    if (rank == -30) {
        simulations =  250;
        handicap = 5;
    } else if (rank == -29) {
        simulations =  500;
        handicap = 5;
    } else if (rank == -28) {
        simulations = 1000;
        handicap = 5;
    } else if (rank == -27) {
        simulations = 2500;
        handicap = 5;
    } else if (rank == -26) {
        simulations = 5000;
        handicap = 5;
    } else if (rank == -25) {
        simulations = 10000;
        handicap = 5;
    } else if (rank == -24) {
        simulations = 250;
        handicap = 4;
    } else if (rank == -23) {
        simulations = 500;
        handicap = 4;
    } else if (rank == -22) {
        simulations = 1000;
        handicap = 4;
    } else if (rank == -21) {
        simulations = 2500;
        handicap = 4;
    } else if (rank == -20) {
        simulations = 5000;
        handicap = 4;
    } else if (rank == -19) {
        simulations = 10000;
        handicap = 4;
    } else if (rank == -18) {
        simulations = 250;
        handicap = 3;
    } else if (rank == -17) {
        simulations = 500;
        handicap = 3;
    } else if (rank == -16) {
        simulations = 1000;
        handicap = 3;
    } else if (rank == -15) {
        simulations = 2500;
        handicap = 3;
    } else if (rank == -14) {
        simulations = 5000;
        handicap = 3;
    } else if (rank == -13) {
        simulations = 10000;
        handicap = 3;
    } else if (rank == -12) {
        simulations = 250;
        handicap = 2;
    } else if (rank == -11) {
        simulations = 500;
        handicap = 2;
    } else if (rank == -10) {
        simulations = 1000;
        handicap = 2;
    } else if (rank == -9) {
        simulations = 2500;
        handicap = 2;
    } else if (rank == -8) {
        simulations = 5000;
        handicap = 2;
    } else if (rank == -7) {
        simulations = 10000;
        handicap = 2;
    } else if (rank == -6) {
        simulations = 250;
        handicap = 0;
    } else if (rank == -5) {
        simulations = 500;
        handicap = 0;
    } else if (rank == -4) {
        simulations = 1000;
        handicap = 0;
    } else if (rank == -3) {
        simulations = 2500;
        handicap = 0;
    } else if (rank == -2) {
        simulations = 5000;
        handicap = 0;
    } else if (rank == -1) {
        simulations = 10000;
        handicap = 0;
    } else if (rank == 0) {
        simulations = 250;
        handicap = -2;
    } else if (rank == 1) {
        simulations = 500;
        handicap = -2;
    } else if (rank == 2) {
        simulations = 1000;
        handicap = -2;
    } else if (rank == 3) {
        simulations = 2500;
        handicap = -2;
    } else if (rank == 4) {
        simulations = 5000;
        handicap = -2;
    } else if (rank == 5) {
        simulations = 10000;
        handicap = -2;
    } else if (rank == 6) {
        simulations = 500;
        handicap = -3;
    } else if (rank == 7) {
        simulations = 1000;
        handicap = -3;
    } else if (rank == 8) {
        simulations = 2500;
        handicap = -3;
    } else if (rank == 9) {
        simulations = 5000;
        handicap = -3;
    } else if (rank == 10) {
        simulations = 10000;
        handicap = -3;
    } else if (rank == 11) {
        simulations = 2500;
        handicap = -4;
    } else if (rank == 12) {
        simulations = 5000;
        handicap = -4;
    } else if (rank == 13) {
        simulations = 10000;
        handicap = -4;
    }
    
    ::wxLogDebug("Handicap %d Simulations %d", handicap, simulations);        
    
    {
        float komi = handicap ? 0.5f : 7.5f;
        m_State.init_game(9, komi);
        m_State.set_fixed_handicap(abs(handicap));        
        // max 60 minutes per game    
        m_State.set_timecontrol(2 * 9 * 60 * 100, 0, 0);
        m_visitLimit = simulations;
        m_playerColor = (handicap >= 0 ? FastBoard::BLACK : FastBoard::WHITE);
        m_panelBoard->setPlayerColor(m_playerColor);
        m_panelBoard->setShowTerritory(false);
        m_ratedGame = true;
    }
        
    m_engineRunning.Post();      
    
    wxCommandEvent myevent(EVT_NEW_MOVE, GetId());
    myevent.SetEventObject(this);                        
    ::wxPostEvent(m_panelBoard->GetEventHandler(), myevent);             
}

void MainFrame::ratedGameEnd(bool won) {
    //wxString mess;
    wxString rankstr;
    
    if (m_ratedGame) {
        int rank = wxConfig::Get()->Read(wxT("LastRank"), (long)-30);
               
        if (won) {
            rank = rank + 1;
            rank = std::min(13, rank);
                                    
            //mess += wxT("Promoting from") + wxString(" ") + rankToString(rank-1) + wxString("\n");
            //mess += wxT("to") + wxString(" ") + rankToString(rank);
        } else {
            rank = rank - 1;
            rank = std::max(-30, rank);
                                    
            //mess += wxT("Demoting from") + wxString(" ") + rankToString(rank+1) + wxString("\n");
            //mess += wxT("to") + wxString(" ") + rankToString(rank);
        }                   
        
//        ::wxMessageBox(mess, wxT("Rated game"), wxOK, this);
        
        wxConfig::Get()->Write(wxT("LastRank"), rank);                        
        
        wxString mess = wxString(_("Rank: "));
        mess += rankToString(rank);    
        m_statusBar->SetStatusText(mess, 1);
        
        // don't adjust rank twice
        m_ratedGame = false;
    }               
}

void MainFrame::scoreGame(bool & won, float & komi, float & handicap, 
                          float & score, float & prekomi) {
    stopEngine();
    m_engineRunning.Wait();        
    
    if (m_State.get_last_move() == FastBoard::RESIGN) {
        komi = m_State.get_komi();
        handicap = m_State.get_handicap();
        
        int size = m_State.board.get_boardsize() * m_State.board.get_boardsize();
        if (m_State.get_to_move() == FastBoard::WHITE) {
            score = -size;
        } else {
            score = size;
        }        
        prekomi = score + komi + handicap;        
    } else {
        komi = m_State.get_komi();
        score = m_State.final_score();
        handicap = m_State.get_handicap();
        prekomi = score + komi + handicap;        
    }      
    
    won = (score > 0.0f && m_playerColor == FastBoard::BLACK)
          || (score < 0.0f && m_playerColor == FastBoard::WHITE);
    
    m_panelBoard->doTerritory();      
    m_panelBoard->setShowTerritory(true);    
    
    m_engineRunning.Post();        
}

bool MainFrame::scoreDialog(float komi, float score, float prekomi, float handicap) {
    wxString mess;
    
    if (score > 0.0f) {        
        if (m_State.get_last_move() == FastBoard::RESIGN) {
            mess.Printf(_("BLACK wins by resignation"));
        } else {
            if (handicap > 0.5f) {
                mess.Printf(_("Final score:\nBLACK wins by %.0f - %.1f (komi) - %0.f (handicap)\n= %.1f points"), prekomi, komi, handicap, score);        
            } else {
                mess.Printf(_("Final score:\nBLACK wins by %.0f - %.1f (komi)\n= %.1f points"), prekomi, komi, score);        
            }
        }            
    } else {
        // avoid minus zero
        prekomi = prekomi - 0.001f;
        score = score - 0.001f;
        if (m_State.get_last_move() == FastBoard::RESIGN) {   
            mess.Printf(_("WHITE wins by resignation"));
        } else {
            if (handicap > 0.5f) {
                mess.Printf(_("Final score:\nWHITE wins by %.0f + %.1f (komi) + %0.f (handicap)\n= %.1f points"), -prekomi, komi, handicap, -score);
            } else {
                mess.Printf(_("Final score:\nWHITE wins by %.0f + %.1f (komi)\n= %.1f points"), -prekomi, komi, -score);
            }
        }            
    }   
    
    int result = ::wxMessageBox(mess, _("Game score"), wxOK | wxCANCEL, this);            
    
    if (result == wxOK) {
        return true;
    } else {
        return false;
    }
}

void MainFrame::doScore(wxCommandEvent& event) {   
    float komi, score, prekomi, handicap;
    bool won;
    
    scoreGame(won, komi, handicap, score, prekomi);
    scoreDialog(komi, score, prekomi, handicap);       
}

wxString MainFrame::rankToString(int rank) {
    wxString res;
    
    if (rank < 0) {
        res.Printf(_("%d kyu"), -rank);
    } else {
        if (rank < 7) {
            res.Printf(_("%d dan"), rank + 1);
        } else {
            res.Printf(_("%d pro"), rank - 6);
        }
    }
    
    return res;
}

void MainFrame::doPass(wxCommandEvent& event) {
    stopEngine();
    m_engineRunning.Wait();

    m_State.play_pass();
    //::wxLogMessage("User passes");
    
    m_engineRunning.Post();
    
    wxCommandEvent myevent(EVT_NEW_MOVE, GetId());
    myevent.SetEventObject(this);                        
    ::wxPostEvent(m_panelBoard->GetEventHandler(), myevent);    
}

void MainFrame::doUndo(wxCommandEvent& event) {
    stopEngine();
    m_engineRunning.Wait();

    if (m_State.undo_move()) {
        ::wxLogDebug("Undoing one move");
    }
    m_playerColor = m_State.get_to_move();
    m_panelBoard->setPlayerColor(m_playerColor);        
    m_panelBoard->setShowTerritory(false);
    
    m_ratedGame = false;
    
    m_engineRunning.Post();
    
    wxCommandEvent myevent(EVT_BOARD_UPDATE, GetId());
    myevent.SetEventObject(this);                        
    ::wxPostEvent(m_panelBoard->GetEventHandler(), myevent);    
}

void MainFrame::doForward(wxCommandEvent& event) {
    stopEngine();
    m_engineRunning.Wait();

    if (m_State.forward_move()) {
        ::wxLogDebug("Forward one move");
    }
    m_playerColor = m_State.get_to_move();
    m_panelBoard->setPlayerColor(m_playerColor);
    m_panelBoard->setShowTerritory(false);
    
    m_engineRunning.Post();
        
    wxCommandEvent myevent(EVT_BOARD_UPDATE, GetId());
    myevent.SetEventObject(this);                        
    ::wxPostEvent(m_panelBoard->GetEventHandler(), myevent);
}

void MainFrame::doBack10(wxCommandEvent& event) {
    for (int i = 0; i < 10; i++) {
        doUndo(event);
    }
}

void MainFrame::doForward10(wxCommandEvent& event) {
    for (int i = 0; i < 10; i++) {
        doForward(event);
    }
}

void MainFrame::doGoRules(wxCommandEvent& event) {
    ::wxLaunchDefaultBrowser("http://senseis.xmp.net/?RulesOfGoIntroductory");
}

void MainFrame::doHomePage(wxCommandEvent& event) {
   ::wxLaunchDefaultBrowser("http://www.sjeng.org/leela");
}

void MainFrame::doHelpAbout(wxCommandEvent& event) {
    AboutDialog myabout(this);
    
    myabout.ShowModal();    
}

void MainFrame::doOpenSGF(wxCommandEvent& event) {    
    stopEngine();
    m_engineRunning.Wait();    
    
    wxString caption = _("Choose a file");
    wxString wildcard = _("Go games (*.sgf)|*.sgf");
    wxFileDialog dialog(this, caption, wxEmptyString, wxEmptyString, wildcard, 
                        wxFD_OPEN | wxFD_CHANGE_DIR | wxFD_FILE_MUST_EXIST);
    
    if (dialog.ShowModal() == wxID_OK) {
        wxString path = dialog.GetPath();
        
        ::wxLogDebug("Opening: " + path);
        
        std::auto_ptr<SGFTree> tree(new SGFTree);
        try {
            tree->load_from_file(path);  
             ::wxLogDebug("Read %d moves", tree->count_mainline_moves());        
            m_State = tree->get_mainline();       
        } catch (...) {
        }
                
        m_playerColor = m_State.get_to_move();
        m_panelBoard->setPlayerColor(m_playerColor);
        m_panelBoard->setShowTerritory(false);
        
        m_engineRunning.Post();
        
        //signal board change
        wxCommandEvent myevent(EVT_BOARD_UPDATE, GetId());
        myevent.SetEventObject(this);                        
        ::wxPostEvent(m_panelBoard->GetEventHandler(), myevent);
    } else {
        m_engineRunning.Post();
    }        
}

void MainFrame::doSaveSGF(wxCommandEvent& event) {
    stopEngine();
    m_engineRunning.Wait();
    
    std::string sgfgame = SGFTree::state_to_string(&m_State, !m_playerColor);
    
    wxString caption = _("Choose a file");
    wxString wildcard = _("Go games (*.sgf)|*.sgf");
    wxFileDialog dialog(this, caption, wxEmptyString, wxEmptyString, wildcard, 
                        wxFD_SAVE | wxFD_CHANGE_DIR | wxFD_OVERWRITE_PROMPT);
                        
    if (dialog.ShowModal() == wxID_OK) {
        wxString path = dialog.GetPath();
        
        ::wxLogDebug("Saving: " + path);
                
        wxFileOutputStream file(path);
        
        if (file.IsOk()) {
            file.Write(sgfgame.c_str(), sgfgame.size());  
        }        
    }        
    
    m_engineRunning.Post();
}

void MainFrame::doForceMove(wxCommandEvent& event) {        
    if (m_engineRunning.TryWait() == wxSEMA_BUSY) {        
        stopEngine();        

        m_analyzing = false;		

        m_engineRunning.Wait();
        m_engineRunning.Post();

        if (m_pondering) {
            m_pondering = false;	
            m_playerColor = !m_State.get_to_move();    
            m_panelBoard->setPlayerColor(m_playerColor);
            m_ratedGame = false;
            m_analyzing = false;			

            startEngine();
        }		
    } else {   
        m_engineRunning.Post();

        m_playerColor = !m_State.get_to_move();    
        m_panelBoard->setPlayerColor(m_playerColor);
    
        m_ratedGame = false;
        m_analyzing = false;
	m_pondering = false;
    
        startEngine();
    }                    
}

void MainFrame::doPassToggle(wxCommandEvent& event) {
    m_passEnabled = !m_passEnabled;
    wxConfig::Get()->Write(wxT("passEnabled"), m_passEnabled);  
}

void MainFrame::doResignToggle(wxCommandEvent& event) {
    m_resignEnabled = !m_resignEnabled;
    wxConfig::Get()->Write(wxT("resignEnabled"), m_resignEnabled);  
}

void MainFrame::doResign(wxCommandEvent& event) {
    if (m_State.get_to_move() == m_playerColor) {
        stopEngine();
        m_engineRunning.Wait();        
            
        m_State.play_move(FastBoard::RESIGN);
        wxCommandEvent myevent(EVT_NEW_MOVE, GetId());
        myevent.SetEventObject(this);                        
        ::wxPostEvent(GetEventHandler(), myevent);
        
        m_engineRunning.Post();
    }
}

void MainFrame::doAnalyze(wxCommandEvent& event) {
    if (m_engineRunning.TryWait() == wxSEMA_BUSY) {
        stopEngine();    

        m_engineRunning.Wait();
        m_engineRunning.Post();

        m_ponderedOnce = true;

        if (m_pondering) {
            m_pondering = false;   
            m_analyzing = true;   
            startEngine();  
        } 
    } else {
        m_engineRunning.Post();

        m_analyzing = true;
	m_pondering = false;
        m_ratedGame = false;
        m_ponderedOnce = true;
                                    
        startEngine();                
    }
}

void MainFrame::doAdjustClocks(wxCommandEvent& event) {
    ClockAdjustDialog mydialog(this);

    mydialog.setTimeControl(*(m_State.get_timecontrol()));

    if (mydialog.ShowModal() == wxID_OK) {
        ::wxLogDebug("Adjust clocks clicked"); 

        m_State.set_timecontrol(mydialog.getTimeControl());       
    } 
}

void MainFrame::doPonderToggle(wxCommandEvent& event) {
    m_ponderEnabled = !m_ponderEnabled;
    if (!m_ponderEnabled) {
        stopEngine();
    }
    wxConfig::Get()->Write(wxT("ponderEnabled"), m_ponderEnabled);  
}
