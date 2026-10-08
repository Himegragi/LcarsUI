#pragma once
#include <string>
#include <vector>
#include <algorithm>

// Platform-independent transaction experiment; offsets are UTF-16 code units.
// The layout/segmentation layer supplies navigation boundaries separately.
namespace editing {
struct State { std::u16string text; unsigned anchor=0,caret=0; };
class Model {
    std::vector<State> undo_stack,redo_stack;
    State before_composition;
public:
    State state;
    bool composing=false;
    std::u16string preedit;
    unsigned preedit_caret=0;
    unsigned begin() const{return std::min(state.anchor,state.caret);}
    unsigned end() const{return std::max(state.anchor,state.caret);}
    void select(unsigned a,unsigned c){if(composing)cancel();state.anchor=std::min(a,(unsigned)state.text.size());state.caret=std::min(c,(unsigned)state.text.size());}
    void replace(std::u16string text){
        if(composing)cancel();undo_stack.push_back(state);redo_stack.clear();
        auto start=begin();state.text.replace(start,end()-start,text);state.anchor=state.caret=start+(unsigned)text.size();
    }
    void set(std::u16string text){if(composing)cancel();state.anchor=0;state.caret=(unsigned)state.text.size();replace(std::move(text));}
    void start(){if(composing)return;before_composition=state;composing=true;preedit.clear();preedit_caret=0;}
    void update(std::u16string value,unsigned caret){if(!composing)start();preedit=std::move(value);preedit_caret=std::min(caret,(unsigned)preedit.size());}
    void commit(std::u16string value){if(!composing)start();state=before_composition;composing=false;preedit.clear();replace(std::move(value));}
    void cancel(){if(!composing)return;state=before_composition;composing=false;preedit.clear();}
    std::u16string visible() const {auto text=state.text;if(composing)text.replace(begin(),end()-begin(),preedit);return text;}
    unsigned visible_caret() const{return composing?begin()+preedit_caret:state.caret;}
    bool undo(){if(composing){cancel();return true;}if(undo_stack.empty())return false;redo_stack.push_back(state);state=undo_stack.back();undo_stack.pop_back();return true;}
    bool redo(){if(composing)return false;if(redo_stack.empty())return false;undo_stack.push_back(state);state=redo_stack.back();redo_stack.pop_back();return true;}
};
}
