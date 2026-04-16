#ifndef D3D11INPUTLAYOUT_H
#define D3D11INPUTLAYOUT_H

#include "main.h"
#include "d3d11devicechild.h"
#include <mutex>

class MyID3D11InputLayout : public ID3D11InputLayout {
    class Impl;
    Impl *impl;

public:
    ID3D11DEVICECHILD_DECL(ID3D11InputLayout);

    MyID3D11InputLayout(
        ID3D11InputLayout **inner,
        const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs,
        UINT NumElements
    );

     ~MyID3D11InputLayout();
    UINT &get_descs_num();
    UINT get_descs_num() const;
    D3D11_INPUT_ELEMENT_DESC *&get_descs();
    D3D11_INPUT_ELEMENT_DESC *get_descs() const;
};

extern std::unordered_map<ID3D11InputLayout*, MyID3D11InputLayout*> cached_ils_map;
extern std::mutex cached_ils_map_mutex;

#endif
