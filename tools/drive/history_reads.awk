# Finds passes in the 2nd full traced frame that read a texture before it is written in that frame,
# but which the previous frame wrote: temporal (history) inputs. Usage: awk -f history_reads.awk trace_<n>.txt
/present \(frame boundary\)/ { frame++; delete written_now; next }
{
    split($0, halves, /\| out:/)
    n = split(halves[1], ins, " ")
    sh = ""
    for (i = 1; i <= n; i++) if (ins[i] == "sh") sh = ins[i + 1]
    if ($3 == "copy") {  # "seq ctx P copy SRC -> DST"
        src = $4; sub(/\(.*/, "", src); dst = $6; sub(/\(.*/, "", dst)
        if (frame == 2 && !(src in written_now) && (src in written_prev)) hist[src " copy"]++
        written_now[dst] = 1
        if (frame == 1) written_prev[dst] = 1
        next
    }
    for (i = 1; i <= n; i++) {
        if (ins[i] !~ /^[0-9]+=/) continue
        split(ins[i], kv, "="); res = kv[2]; desc = res; sub(/\(.*/, "", res); sub(/^[^(]*/, "", desc)
        if (frame == 2 && !(res in written_now) && (res in written_prev)) {
            key = sh " slot" kv[1] " " res desc
            if (!(key in seen)) { seen[key] = $1; order[++no] = key }
        }
    }
    m = split(halves[2], outs, " ")
    for (i = 1; i <= m; i++) {
        if (outs[i] !~ /=/) continue
        split(outs[i], kv, "="); res = kv[2]; sub(/\(.*/, "", res)
        written_now[res] = 1
        if (frame == 1) written_prev[res] = 1
    }
}
END { for (i = 1; i <= no; i++) print seen[order[i]], order[i] }
