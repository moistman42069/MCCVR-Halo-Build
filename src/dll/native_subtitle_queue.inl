// Worker-owned, fixed two-line composition, shared with the queue fixture.
struct Event {
    GameTitle title=GameTitle::None;
    uint32_t generation=0,length=0;
    uint64_t expires=0,sequence=0;
    bool theatre=false;
    uint8_t channel=0;
    wchar_t text[subtitles::kTextCapacity]{};
};
struct RasterCaption {
    GameTitle title=GameTitle::None;
    uint32_t generation=0,length=0;
    uint64_t expires=0;
    bool theatre=false;
    wchar_t text[subtitles::kTextCapacity*2]{};
};
Event g_captionLines[2]{};
bool g_captionPair=false;
void AcceptSubtitleEvent(const Event& event) noexcept {
    if(event.channel>2) return;
    if(event.channel==2) {
        const auto& first=g_captionLines[0];
        if(!g_captionPair||first.title!=event.title||first.generation!=event.generation||
            first.theatre!=event.theatre||first.sequence>=event.sequence) return;
        g_captionLines[1]=event;
    } else {
        g_captionLines[0]=event;g_captionLines[1]={};g_captionPair=event.channel==1;
    }
}
bool ComposeSubtitleCaption(GameTitle title,uint32_t generation,bool theatre,uint64_t now,
    RasterCaption& caption) noexcept {
    caption={};caption.title=title;caption.generation=generation;caption.theatre=theatre;
    for(auto& line:g_captionLines) {
        if(!subtitles::Current(line.title,line.generation,line.expires,title,generation,now)||line.theatre!=theatre) {
            line={};continue;
        }
        wchar_t normalized[subtitles::kTextCapacity]{};
        if(!subtitles::Normalize(line.text,line.length,normalized,_countof(normalized))) continue;
        size_t length=0;while(length<_countof(normalized)&&normalized[length]) ++length;
        if(caption.length) caption.text[caption.length++]=L'\n';
        for(size_t i=0;i<length;++i) caption.text[caption.length++]=normalized[i];
        caption.text[caption.length]=0;
        if(!caption.expires||line.expires<caption.expires) caption.expires=line.expires;
    }
    return caption.length!=0;
}
