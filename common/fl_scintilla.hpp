#include <string>
#include <unordered_map>

#include <FL/Fl_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Browser.H>
#include "Fl_Scintilla.h"
    
#include "general.hpp"

inline void child_to_local(Fl_Group* wd) {
    int gx = wd->x();
    int gy = wd->y();

    if (wd->parent()) {
        gx += wd->parent()->x();
        gy += wd->parent()->y();
    } 
    for (int i = 0; i < wd->children(); ++i) {
        Fl_Widget *o = wd->child(i);
        // cotm(o->label());
        o->position(o->x() + gx, o->y() + gy);
    }
}

struct FileEntry {
    std::string filename;
    std::time_t modified;

    // Sort by modified time (ascending)
    bool operator<(const FileEntry& other) const {
        return modified > other.modified;
    }

    // Sort by filename (lexicographically)
    bool operator==(const FileEntry& other) const {
        return filename == other.filename;
    }
};
 struct fl_scintilla : public Fl_Scintilla {  
    std::string filename="";
    sptr_t curr_file_pointer=0;
    std::string comment;
    std::unordered_map<sptr_t,uptr_t> filesfirstline;  
    // std::unordered_map<sptr_t,uptr_t> filescaret;  
    fl_scintilla(int X, int Y, int W, int H, const char* l = 0);
    // void resize(int x, int y, int w, int h) override;
    int handle(int e)override;
    void save();
    void setnsaved();
    void set_lua(); 
    Fl_Group * navigator;
    Fl_Window* toggleSearchGroup; 
    void helperinit();
    void searchshow();
    void searchhide(); 
    Fl_Browser* bfiles;
    Fl_Browser* bfilesmodified;
    Fl_Browser* bfunctions;
    vint vline;
    void getfuncs();
    void navigatorSetUpdated();
    std::vector<FileEntry> lfiles;
    void find(bool dir);
};