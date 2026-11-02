#include "dynos_voice_manifest.h"
#include <cctype>
#include <cstdlib>
#include <utility>
extern "C" {
#include "sounds.h"
}
namespace dynos {
namespace {
struct Token {enum Kind {Identifier, String, Symbol, Number} kind;std::string text;};
struct Value {
    enum Kind {Invalid, String, Reference, Model, Table} kind=Invalid;
    std::string text;
    std::vector<Value> items;
    std::map<std::string,Value> fields;
};
struct Parser {
    std::vector<Token> tokens;
    size_t at=0;
    bool valid=true;
    bool is(const char *s) const {return at<tokens.size()&&tokens[at].text==s&&tokens[at].kind==Token::Symbol;}
    bool take(const char *s){if(is(s)){++at;return true;}return false;}
    bool lex(const std::string &s) {
        for(size_t i=0;i<s.size();) {
            unsigned char c=s[i];
            if(std::isspace(c)){++i;continue;}
            bool comment=i+1<s.size()&&s.compare(i,2,"--")==0;
            if(comment)i+=2;
            // Lua long comments/strings, including [=[ ... ]=].
            size_t begin=i;
            if(i<s.size()&&s[i]=='[') {
                size_t j=i+1;while(j<s.size()&&s[j]=='=')++j;
                if(j<s.size()&&s[j]=='[') {
                    std::string close="]"+s.substr(i+1,j-i-1)+"]";
                    size_t end=s.find(close,j+1);if(end==std::string::npos)return false;
                    if(!comment)tokens.push_back({Token::String,s.substr(j+1,end-j-1)});
                    i=end+close.size();continue;
                }
            }
            if(comment){i=s.find('\n',i);if(i==std::string::npos)break;continue;}
            i=begin;c=s[i];
            if(c=='\''||c=='"') {
                char quote=c;std::string text;++i;bool closed=false;
                while(i<s.size()) {
                    char next=s[i++];if(next==quote){closed=true;break;}
                    if(next=='\\') {
                        if(i==s.size())return false;
                        next=s[i++];
                        if(next=='n')next='\n';else if(next=='r')next='\r';else if(next=='t')next='\t';
                        else if(next!='\\'&&next!='\''&&next!='"')return false;
                    }
                    text+=next;if(text.size()>4096)return false;
                }
                if(!closed)return false;tokens.push_back({Token::String,text});
            } else if(std::isalpha(c)||c=='_') {
                size_t j=i++;while(i<s.size()&&(std::isalnum(static_cast<unsigned char>(s[i]))||s[i]=='_'))++i;
                tokens.push_back({Token::Identifier,s.substr(j,i-j)});
            } else if(std::isdigit(c)) {
                size_t j=i++;while(i<s.size()&&(std::isalnum(static_cast<unsigned char>(s[i]))||s[i]=='.'))++i;
                tokens.push_back({Token::Number,s.substr(j,i-j)});
            } else {tokens.push_back({Token::Symbol,std::string(1,c)});++i;}
            if(tokens.size()>262144)return false;
        }
        return true;
    }
    Value value(unsigned depth=0) {
        Value v;if(at>=tokens.size()||depth>32){valid=false;return v;}
        if(take("{")) {
            v.kind=Value::Table;
            while(at<tokens.size()&&!is("}")) {
                size_t before=at;std::string key;
                if(take("[")) {
                    if(at<tokens.size()&&(tokens[at].kind==Token::Identifier||tokens[at].kind==Token::String))key=tokens[at++].text;
                    else {while(at<tokens.size()&&!is("]"))++at;}
                    if(!take("]")||!take("=")){valid=false;return {};}
                } else if(at+1<tokens.size()&&tokens[at].kind==Token::Identifier&&tokens[at+1].text=="=") {key=tokens[at].text;at+=2;}
                Value item=expression(depth+1);
                if(key.empty())v.items.push_back(std::move(item));else v.fields[key]=std::move(item);
                if(take(",")||take(";"))continue;
                if(!is("}")||at==before){valid=false;return {};}
            }
            if(!take("}"))valid=false;
        } else if(tokens[at].kind==Token::String) {v.kind=Value::String;v.text=tokens[at++].text;}
        else if(tokens[at].kind==Token::Identifier) {
            v.kind=Value::Reference;v.text=tokens[at++].text;
            if(take("(")) {
                std::string function=v.text;Value arg=expression(depth+1);
                bool close=take(")");
                if(close&&arg.kind==Value::String&&(function=="audio_sample_load"||function=="smlua_model_util_get_id")) {
                    v=std::move(arg);if(function=="smlua_model_util_get_id")v.kind=Value::Model;
                } else {v={};if(!close)skip_expression();}
            }
        } else {++at;}
        return v;
    }
    void skip_expression() {
        unsigned nesting=0;
        while(at<tokens.size()) {
            if(!nesting&&(is(",")||is(";")||is("}")||is(")")))return;
            if(is("(")||is("{")||is("["))++nesting;
            else if(is(")")||is("}")||is("]")){if(!nesting)return;--nesting;}
            ++at;
        }
    }
    Value expression(unsigned depth=0) {
        Value v=value(depth);
        if(at<tokens.size()&&!is(",")&&!is(";")&&!is("}")&&!is(")")) {
            // Operators or arbitrary calls are not evaluated. Do not mistake a
            // string prefix of a dynamic expression for a literal filename.
            v={};skip_expression();
        }
        return v;
    }
};
Value resolve(Value v,const std::map<std::string,Value> &variables,unsigned depth=0) {
    if(v.kind!=Value::Reference)return v;
    auto found=variables.find(v.text);
    return depth>=16||found==variables.end()?Value{}:resolve(found->second,variables,depth+1);
}
bool extract(const Value &table,const std::map<std::string,Value> &variables,VoiceManifest &out) {
    if(table.kind!=Value::Table)return false;
    for(const auto &field:table.fields) {
        if(field.first.compare(0,11,"CHAR_SOUND_"))continue;
        Value v=resolve(field.second,variables);std::vector<std::string> files;
        if(v.kind==Value::String&&voice_relative_path(v.text))files.push_back(v.text);
        if(v.kind==Value::Table&&v.fields.empty()&&v.items.size()<=16) {
            bool valid=!v.items.empty();
            for(const Value &item:v.items){Value x=resolve(item,variables);if(x.kind!=Value::String||!voice_relative_path(x.text)){valid=false;break;}files.push_back(x.text);}
            if(!valid)files.clear();
        }
        if(!files.empty())out[field.first]=std::move(files);
    }
    return !out.empty();
}
}
bool voice_relative_path(const std::string &path) {
    if(path.empty()||path.size()>240||path[0]=='/'||path.find('\\')!=std::string::npos||path.find(':')!=std::string::npos)return false;
    for(unsigned char c:path)if(c<32||c==127)return false;
    size_t start=0;
    do {size_t end=path.find('/',start);std::string part=path.substr(start,end-start);if(part.empty()||part=="."||part=="..")return false;if(end==std::string::npos)break;start=end+1;}while(true);
    return true;
}
bool voice_manifest(const std::string &lua,const std::string &model,VoiceManifest &out,std::string &error) {
    out.clear();error.clear();Parser p;
    if(lua.size()>1024*1024||!p.lex(lua)){error="invalid or oversized Lua data";return false;}
    std::map<std::string,Value> variables;
    for(size_t i=0;i+2<p.tokens.size();++i) {
        if(p.tokens[i].kind!=Token::Identifier||p.tokens[i+1].text!="="||(i&&p.tokens[i-1].text=="."))continue;
        p.at=i+2;Value v=p.value();
        // Assignment data ends at the parsed literal. Operators invalidate it.
        if(p.at<p.tokens.size()&&p.tokens[p.at].kind==Token::Symbol&&p.tokens[p.at].text!=";"&&p.tokens[p.at].text!=","&&p.tokens[p.at].text!="}"&&p.tokens[p.at].text!=")")v={};
        auto inserted=variables.emplace(p.tokens[i].text,std::move(v));
        if(!inserted.second)inserted.first->second={}; // ambiguous/dynamic assignment
        if(!p.valid){error="malformed Lua table";return false;}
    }
    bool has_registration=false;unsigned matches=0;
    for(size_t i=0;i+1<p.tokens.size();++i) {
        if(p.tokens[i].kind!=Token::Identifier||p.tokens[i].text!="character_add_voice"||p.tokens[i+1].text!="(")continue;
        has_registration=true;p.at=i+2;Value target=p.expression();if(!p.take(","))continue;Value table=p.expression();if(!p.take(")"))continue;
        target=resolve(target,variables);table=resolve(table,variables);
        if(target.kind==Value::Model&&target.text==model){++matches;extract(table,variables,out);}
    }
    if(matches>1){out.clear();error="ambiguous model voice registrations";return false;}
    if(!has_registration) {
        unsigned count=0;
        for(const auto &variable:variables){VoiceManifest candidate;if(extract(resolve(variable.second,variables),variables,candidate)){++count;out=std::move(candidate);}}
        if(count>1){out.clear();error="multiple voice tables require character_add_voice association";return false;}
    }
    if(out.empty()){error="no static voice table for selected model";return false;}
    return true;
}
std::vector<PlayerDeclaration> player_declarations(const std::string &lua) {
    Parser p;std::vector<PlayerDeclaration> result;
    if(lua.size()>1024*1024||!p.lex(lua))return result;
    std::map<std::string,Value> variables;
    for(size_t i=0;i+2<p.tokens.size();++i) {
        if(p.tokens[i].kind!=Token::Identifier||p.tokens[i+1].text!="="||(i&&p.tokens[i-1].text=="."))continue;
        p.at=i+2;Value v=p.value();
        if(p.at<p.tokens.size()&&p.tokens[p.at].kind==Token::Symbol&&p.tokens[p.at].text!=";"&&p.tokens[p.at].text!=","&&p.tokens[p.at].text!="}"&&p.tokens[p.at].text!=")")v={};
        auto inserted=variables.emplace(p.tokens[i].text,std::move(v));if(!inserted.second)inserted.first->second={};
        if(!p.valid)return {};
    }
    for(size_t i=0;i+1<p.tokens.size();++i) {
        if(p.tokens[i].kind!=Token::Identifier||p.tokens[i+1].text!="(")continue;
        bool costume=p.tokens[i].text=="character_add_costume";
        if(!costume&&p.tokens[i].text!="character_add")continue;
        p.at=i+2;std::vector<Value> args;
        while(p.at<p.tokens.size()&&!p.is(")")&&args.size()<16){args.push_back(p.expression());if(!p.take(","))break;}
        if(!p.take(")"))continue;
        unsigned offset=costume?1:0;
        if(args.size()<=offset+4)continue;
        Value model=resolve(args[offset+4],variables),name=resolve(args[offset],variables);
        if(model.kind!=Value::Model||model.text.size()<5||model.text.substr(model.text.size()-4)!="_geo"||!voice_relative_path(model.text)||model.text.find('/')!=std::string::npos)continue;
        bool duplicate=false;for(const auto &entry:result)if(entry.model==model.text)duplicate=true;
        if(duplicate||result.size()==64)continue;
        PlayerDeclaration entry;entry.model=model.text;
        if(name.kind==Value::String)entry.name=name.text;
        while(!entry.name.empty()&&std::isspace(static_cast<unsigned char>(entry.name.back())))entry.name.pop_back();
        if(args.size()>offset+5&&args[offset+5].kind==Value::Reference){
            const std::string &base=args[offset+5].text;
            if(base=="CT_LUIGI")entry.slot="luigi";else if(base=="CT_WARIO")entry.slot="wario";
            else if(base=="CT_WALUIGI")entry.slot="waluigi";else if(base=="CT_TOAD")entry.slot="toad_player";
        }
        result.push_back(std::move(entry));
    }
    for(size_t i=0;i+1<p.tokens.size();++i) {
        if(p.tokens[i].kind!=Token::Identifier||p.tokens[i].text!="character_add_palette_preset"||p.tokens[i+1].text!="(")continue;
        p.at=i+2;Value model=p.expression();if(!p.take(","))continue;Value table=p.expression();if(!p.take(")"))continue;
        model=resolve(model,variables);table=resolve(table,variables);
        if(model.kind!=Value::Model||table.kind!=Value::Table)continue;
        for(auto &entry:result)if(entry.model==model.text&&!entry.has_palette){
            static const char *const parts[]={"PANTS","SHIRT","GLOVES","SHOES","HAIR","SKIN","CAP","EMBLEM"};
            bool valid=true;
            for(unsigned part=0;part<8;++part){
                auto field=table.fields.find(parts[part]);Value color=field==table.fields.end()?Value{}:resolve(field->second,variables);
                if(color.kind!=Value::String||color.text.size()!=6){valid=false;break;}
                for(unsigned char c:color.text)if(!std::isxdigit(c))valid=false;
                if(!valid)break;entry.palette[part]=std::strtoul(color.text.c_str(),nullptr,16);
            }
            entry.has_palette=valid; // first complete static preset is the default
        }
    }
    return result;
}

std::string voice_sound_key(unsigned bits) {
    unsigned bank=(bits&SOUNDARGS_MASK_BANK)>>SOUNDARGS_SHIFT_BANK;
    unsigned id=(bits>>SOUNDARGS_SHIFT_SOUNDID)&255;
    if(bank==SOUND_BANK_MENU&&id==0x24)return "CHAR_SOUND_LETS_A_GO";
    if(bank!=SOUND_BANK_VOICE)return {};
    if(id<3)return "CHAR_SOUND_YAH_WAH_HOO";
    if(id>=0x2b&&id<=0x2f)return "CHAR_SOUND_YAHOO_WAHA_YIPPEE";
    if(id>=0x16&&id<=0x17)return "CHAR_SOUND_PANTING_COLD";
    if(id>=0x18&&id<=0x1a)return "CHAR_SOUND_PANTING";
    unsigned priority=(bits&SOUNDARGS_MASK_PRIORITY)>>SOUNDARGS_SHIFT_PRIORITY;
    if(id==0x0b&&priority>=0xd0)return "CHAR_SOUND_OOOF2";
    if(id==0x11&&priority>=0xf0)return "CHAR_SOUND_HAHA_2";
    if(id==0x13)return priority>=0xd0?"CHAR_SOUND_UH2_2":"CHAR_SOUND_UH2";
    static const char *const keys[0x38]={
        nullptr,nullptr,nullptr,"HOOHOO","YAHOO","UH","HRMM","WAH2","WHOA","EEUH","ATTACKED","OOOF","HERE_WE_GO","YAWNING","SNORING1","SNORING2",
        "WAAAOOOW","HAHA",nullptr,nullptr,"ON_FIRE","DYING",nullptr,nullptr,nullptr,nullptr,nullptr,"COUGHING1","COUGHING2","COUGHING3","PUNCH_YAH","PUNCH_HOO",
        "MAMA_MIA","OKEY_DOKEY","GROUND_POUND_WAH","DROWNING","PUNCH_WAH",nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,
        "DOH","GAME_OVER","HELLO","PRESS_START_TO_PLAY","TWIRL_BOUNCE","SNORING3","SO_LONGA_BOWSER","IMA_TIRED"};
    return id<0x38&&keys[id]?std::string("CHAR_SOUND_")+keys[id]:std::string();
}
}
