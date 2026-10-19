#include "card_memory.hpp"
#include <algorithm>

namespace hive::features::collection {
namespace {
bool revealed(const game::CardImage& image) {
    // The cropped card back is flat color. Compare against that template with
    // its average color removed, tolerating hover, cursor and capture noise.
    constexpr unsigned pixels=game::CardImage{}.size()/3;
    unsigned difference=0;
    for (unsigned channel=0;channel<3;++channel) {
        unsigned total=0;
        for (unsigned i=channel;i<image.size();i+=3) total+=image[i];
        for (unsigned i=channel;i<image.size();i+=3) difference+=std::abs(int(image[i]*pixels)-int(total));
    }
    return difference>12*image.size()*pixels;
}
}
void CardMemory::begin(const game::MemoryBoard& board) {
    if (board.cards.size()<4 || board.cards.size()>36) throw std::invalid_argument("Invalid memory board");
    slots_.clear(); open_.clear(); waiting_.reset();
    for (const auto& card:board.cards) slots_.push_back({card.bounds,{},{}});
    observe(board);
}
void CardMemory::observe(const game::MemoryBoard& board) {
    std::vector<unsigned> opened;
    for (unsigned i=0;i<slots_.size();++i) {
        auto& slot=slots_[i];
        const auto found=std::find_if(board.cards.begin(),board.cards.end(),[&](const auto& card) {
            return std::abs(card.bounds.x-slot.bounds.x)<slot.bounds.width*.2 && std::abs(card.bounds.y-slot.bounds.y)<slot.bounds.height*.2;
        });
        if (found==board.cards.end()) continue;
        if (!slot.matched && revealed(found->image)) { slot.face=found->image; opened.push_back(i); }
    }
    if (waiting_ && std::find(opened.begin(),opened.end(),*waiting_)!=opened.end()) waiting_.reset();
    for (unsigned a=0;a<opened.size();++a) for (unsigned b=a+1;b<opened.size() && !slots_[opened[a]].matched;++b)
        if (!slots_[opened[b]].matched && game::card_distance(*slots_[opened[a]].face,*slots_[opened[b]].face)<6)
            slots_[opened[a]].matched=slots_[opened[b]].matched=true;
    std::erase_if(opened,[&](unsigned i) { return slots_[i].matched; });
    open_=std::move(opened);
}
std::optional<unsigned> CardMemory::choose() const {
    if (waiting_ || open_.size()>1) return {};
    if (open_.size()==1) {
        const auto first=open_.front();
        for (unsigned i=0;i<slots_.size();++i)
            if (i!=first && !slots_[i].matched && slots_[i].face && game::card_distance(*slots_[i].face,*slots_[first].face)<6) return i;
    } else for (unsigned i=0;i<slots_.size();++i) if (!slots_[i].matched && slots_[i].face)
        for (unsigned j=i+1;j<slots_.size();++j)
            if (!slots_[j].matched && slots_[j].face && game::card_distance(*slots_[i].face,*slots_[j].face)<6) return i;
    for (unsigned i=0;i<slots_.size();++i) if (!slots_[i].matched && !slots_[i].face) return i;
    for (unsigned i=0;i<slots_.size();++i)
        if (!slots_[i].matched && std::find(open_.begin(),open_.end(),i)==open_.end()) return i;
    return {};
}
void CardMemory::clicked(unsigned index) {
    if (index>=slots_.size() || slots_[index].matched || waiting_ || std::find(open_.begin(),open_.end(),index)!=open_.end())
        throw std::invalid_argument("Memory card is not selectable");
    waiting_=index;
}
Point CardMemory::center(unsigned index) const {
    const auto& box=slots_.at(index).bounds; return {box.x+box.width/2,box.y+box.height/2};
}
bool CardMemory::complete() const { return !slots_.empty() && std::all_of(slots_.begin(),slots_.end(),[](const auto& slot) { return slot.matched; }); }
}
