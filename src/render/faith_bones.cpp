// Faith's bone matrices (Stage 7). See faith_bones.h.
#include "render/faith_bones.h"

#include <d3d11_1.h>
#include <windows.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "diagnostics/log.h"

namespace mecvr::faith_bones {
namespace {

constexpr unsigned kRingBytes = 1048576;
constexpr unsigned kHeaderRows = 9;  // lighting rows before a piece's bones

std::atomic<int> g_poke_piece{0}, g_poke_bone{-1};
std::atomic<int> g_record_left{0};
std::filesystem::path g_dir;
FILE* g_rec = nullptr;
int g_recorded = 0;
std::mutex g_mutex;
std::unordered_map<ID3D11Resource*, std::vector<unsigned char>> g_ring;  // last contents of each 1 MB CB ring
std::unordered_map<ID3D11Resource*, std::vector<unsigned char>> g_bonebuf;  // last contents of the bone buffers

struct Piece { ID3D11Resource* buf; unsigned start, count; unsigned long long vb; };
std::vector<Piece> g_pieces;      // this frame's Faith pieces, in draw order
std::vector<Piece> g_last_pieces;  // the previous frame's (log)

int piece_index(ID3D11Resource* buf, unsigned start) {
    for (size_t i = 0; i < g_pieces.size(); ++i)
        if (g_pieces[i].buf == buf && g_pieces[i].start == start) return int(i);
    return -1;
}

}  // namespace

void set_poke(int piece, int bone) {
    g_poke_piece = piece;
    g_poke_bone = bone;
}
bool active() { return g_poke_piece.load() != 0 || g_record_left.load() > 0; }

void request_record(const std::filesystem::path& dir, int frames) {
    std::lock_guard lock(g_mutex);
    g_dir = dir;
    g_recorded = 0;
    g_record_left = frames;
}

void on_cb_write(ID3D11Resource* res, const void* data, unsigned size) {
    if (!active() || size != kRingBytes) return;
    std::lock_guard lock(g_mutex);
    auto& v = g_ring[res];
    v.resize(size);
    memcpy(v.data(), data, size);
}

void on_bone_write(ID3D11Resource* res, const void* data, unsigned size) {
    if (g_record_left.load() <= 0 || size != kRingBytes) return;
    std::lock_guard lock(g_mutex);
    auto& v = g_bonebuf[res];
    v.resize(size);
    memcpy(v.data(), data, size);
}

void before_draw(ID3D11DeviceContext* ctx) {
    if (!active()) return;
    ID3D11DepthStencilState* dss = nullptr;
    UINT ref = 0;
    ctx->OMGetDepthStencilState(&dss, &ref);
    if (!dss) return;
    D3D11_DEPTH_STENCIL_DESC dd;
    dss->GetDesc(&dd);
    dss->Release();
    if (!dd.StencilEnable || ref != 0x20) return;  // Faith's body (D-036)

    ID3D11DeviceContext1* c1 = nullptr;
    if (FAILED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&c1)))) return;
    ID3D11Buffer* cb = nullptr;
    UINT first = 0, num = 0;
    c1->VSGetConstantBuffers1(0, 1, &cb, &first, &num);
    c1->Release();
    if (!cb) return;
    unsigned start = 0, count = 0;
    {
        std::lock_guard lock(g_mutex);
        auto it = g_ring.find(cb);
        if (it != g_ring.end() && (first + 1) * 16 <= it->second.size()) {
            const unsigned* u = reinterpret_cast<const unsigned*>(it->second.data() + first * 16);
            start = u[0];
            count = u[1];
        }
    }
    cb->Release();
    if (count <= kHeaderRows || (count - kHeaderRows) % 3) return;

    ID3D11ShaderResourceView* srv = nullptr;
    ctx->VSGetShaderResources(0, 1, &srv);
    if (!srv) return;
    ID3D11Resource* buf = nullptr;
    srv->GetResource(&buf);
    D3D11_SHADER_RESOURCE_VIEW_DESC vd;
    srv->GetDesc(&vd);
    srv->Release();
    if (!buf) return;
    ID3D11Buffer* vb = nullptr;
    UINT stride = 0, offset = 0;
    ctx->IAGetVertexBuffers(0, 1, &vb, &stride, &offset);
    unsigned long long vb_id = vb ? reinterpret_cast<unsigned long long>(vb) ^ offset : 0;
    if (vb) vb->Release();

    int idx;
    {
        std::lock_guard lock(g_mutex);
        idx = piece_index(buf, start);
        if (idx >= 0) {  // already handled this frame (the same piece drawn again: shadows, other passes)
            buf->Release();
            return;
        }
        g_pieces.push_back({buf, start, count, vb_id});
        idx = int(g_pieces.size()) - 1;
    }
    if (idx + 1 == g_poke_piece.load()) {
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(ctx->Map(buf, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &m)) && m.pData) {
            float* rows = reinterpret_cast<float*>(static_cast<unsigned char*>(m.pData) +
                                                   (size_t(vd.Buffer.FirstElement) + start + kHeaderRows) * 16);
            const int bones = int(count - kHeaderRows) / 3, only = g_poke_bone.load();
            for (int b = 0; b < bones; ++b)
                if (only < 0 || only == b) rows[b * 12 + 4 + 3] += 0.3f;  // row 1 (y) translation: up 30 cm
            ctx->Unmap(buf, 0);
        }
    }
    buf->Release();
}

void on_present() {
    if (!active()) return;
    static ULONGLONG last = 0;
    std::lock_guard lock(g_mutex);
    if (g_record_left.load() > 0 && !g_pieces.empty()) {
        // The biggest piece (the arms, 81 bones in PLAYTEST-era recon) from the bone buffer's copy.
        const Piece* big = &g_pieces[0];
        for (const Piece& p : g_pieces)
            if (p.count > big->count) big = &p;
        auto it = g_bonebuf.find(big->buf);
        const size_t from = (size_t(big->start) + kHeaderRows) * 16;
        const unsigned bones = (big->count - kHeaderRows) / 3;
        if (it != g_bonebuf.end() && from + bones * 48 <= it->second.size()) {
            if (!g_rec) _wfopen_s(&g_rec, (g_dir / L"bones_rec.bin").c_str(), L"wb");
            if (g_rec) {
                fwrite(&bones, 4, 1, g_rec);
                fwrite(it->second.data() + from, 48, bones, g_rec);
                ++g_recorded;
            }
        }
        if (--g_record_left <= 0 && g_rec) {
            fclose(g_rec);
            g_rec = nullptr;
            LOG_I("bones", "bones_rec.bin written: %d frames of the %u-bone piece", g_recorded, bones);
        }
    }
    if (GetTickCount64() - last >= 1000 && !g_pieces.empty()) {
        last = GetTickCount64();
        char line[512];
        size_t len = 0;
        for (size_t i = 0; i < g_pieces.size() && len < sizeof(line) - 48; ++i)
            len += snprintf(line + len, sizeof(line) - len, " %zu:%u+%u(%u bones, mesh %llx)", i + 1, g_pieces[i].start,
                            g_pieces[i].count, (g_pieces[i].count - kHeaderRows) / 3, g_pieces[i].vb & 0xffffff);
        LOG_I("bones", "Faith pieces this frame:%s; poking piece %d bone %d", line, g_poke_piece.load(),
              g_poke_bone.load());
    }
    g_last_pieces.swap(g_pieces);
    g_pieces.clear();
}

}  // namespace mecvr::faith_bones
