import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    implicitWidth: 1000
    implicitHeight: 760

    // ---- design tokens
    readonly property color void_: "#0a0b08"
    readonly property color panel: "#14160f"
    readonly property color raised: "#1c1f15"
    readonly property color chart: "#ccff33"
    readonly property color chartDim: "#5c6b26"
    readonly property color ash: "#8a9270"
    readonly property color danger: "#ff4d4d"
    readonly property color paper: "#eaffc2"
    readonly property string mono: "monospace"

    // ---- vector icons drawn with Canvas (image files/data URIs do not load inside Basecamp)
    readonly property var icoLogo: @@ICO_LOGO@@
    readonly property var icoGithub: @@ICO_GITHUB@@
    readonly property var icoX: @@ICO_X@@

    // ---- version and links (empty link = the button is hidden)
    readonly property string appVersion: "@@VERSION@@"
    readonly property string urlGithub: "@@URL_GITHUB@@"
    readonly property string urlX: "@@URL_X@@"

    // ---- state
    // ---- translations (English is the fallback)
    property string lang: "en"
    readonly property var langs: ["en", "es", "fr", "de", "pt", "ru", "zh", "ja", "ar", "tr", "ko", "it", "id", "hi"]
    readonly property var langNames: @@NATIVE@@
    readonly property var langNamesIn: @@LANGNAMES@@
    // name of a language in the current UI language, plus its own name when different (so it can always be found)
    function langLabel(c) {
        var a = langNamesIn[lang] ? langNamesIn[lang][c] : langNames[c], n = langNames[c]
        return a === n ? n : a + " · " + n
    }
    readonly property var i18n: (@@I18N@@)
    function tr(k) {
        var d = (i18n[lang] && i18n[lang][k] !== undefined) ? i18n[lang][k] : (i18n.en[k] !== undefined ? i18n.en[k] : k)
        var args = arguments
        return d.replace(/\{(\d)\}/g, function (m, i) { var v = args[parseInt(i) + 1]; return v !== undefined ? v : "" })
    }
    function fmtUp(s) {
        s = Math.max(0, Math.floor(s))
        var h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), x = s % 60
        return h > 0 ? h + "h " + (m < 10 ? "0" : "") + m + "m" : (m > 0 ? m + "m " + (x < 10 ? "0" : "") + x + "s" : x + "s")
    }

    property var bins: ({})
    property var upd: ({})
    property string updSkip: ""
    property bool updHidden: false
    property bool tempLimitOn: true
    property int tempLimit: 85
    property bool silentMode: false
    property bool pauseBattery: false
    property bool fallbackPublic: true
    property int zeroTicks: 0
    property var hw: ({ cores: 0, logical: 0, ramGb: 0, hugePages: false, msr: false, recommended: 0 })
    property bool hwOk: false
    property bool hwTried: false
    property string hwErr: ""
    property string sysErr: ""
    property int savedThreads: 0
    readonly property real histCur: history.length ? history[history.length - 1] : 0
    readonly property real histPeak: { var m = 0; for (var i = 0; i < history.length; ++i) m = Math.max(m, history[i]); return m }
    readonly property real histAvg: { var s = 0; for (var i = 0; i < history.length; ++i) s += history[i]; return history.length ? s / history.length : 0 }
    property var sys: ({ cpu: -1, ram: -1, temp: -1 })
    property var st: ({ phase: "idle", node: "", hashrate: "", h60: "", h15: "", uptime: 0, paused: false, pauseReason: "", shares: "", p2poolShares: 0, error: "", running: false })
    property var scan: ({ running: false, valid: false, count: 0, probed: 0, rpcFail: 0, zmqFail: 0, nodes: [] })
    property string notice: ""
    property bool noticeBad: false
    property int logNext: 0
    property int logCount: 0
    property bool busyInstall: false
    property bool running: false
    property bool showWallet: false
    property bool loaded: false
    property var history: []
    property int tickN: 0
    property string nodeType: "public"       // public | manual
    property string sidechain: "mini"
    property bool lightMode: false
    property string binDir: ""
    readonly property string defaultNodes: "node.sethforprivacy.com:18081:18083\nnode.moneroworld.com:18089:18083\nxmrnode.cakewallet.com:18081:18083\nopennode.xmr-tw.org:18089:18083"

    readonly property bool walletValid: /^4[0-9A-Za-z]{94}$/.test(walletField.text.trim())
    readonly property bool binsReady: !!bins.xmrig && !!bins.p2pool && bins.xmrig.present && bins.p2pool.present
    readonly property string phase: st.phase || "idle"
    readonly property color phaseColor: phase === "mining" ? chart : (phase === "error" ? danger : (running ? "#e6c84a" : chartDim))
    readonly property string updText: {
        var a = []
        if (upd.xmrig && upd.xmrig.newer) a.push("XMRig " + upd.xmrig.cur + " → " + upd.xmrig.latest)
        if (upd.p2pool && upd.p2pool.newer) a.push("P2Pool " + upd.p2pool.cur + " → " + upd.p2pool.latest)
        return a.join(" · ")
    }
    readonly property bool updShow: updText !== "" && !updHidden && updSkip !== updText && !running

    // callModule returns the module's string JSON-quoted; our replies are JSON text inside it.
    function rpc(method, args) {
        if (typeof logos === "undefined" || !logos.callModule)
            return { error: "Logos bridge not available" }
        var raw
        try { raw = String(logos.callModule("hushrate_core", method, args)) }
        catch (e) { return { error: "EXCEPTION: " + e } }
        var v
        try { v = JSON.parse(raw) } catch (e1) { return { error: raw } }
        if (typeof v === "string") {
            try { v = JSON.parse(v) } catch (e2) {
                // module builds printed decimals with the system locale ("12,4"): repair and retry
                try { v = JSON.parse(v.replace(/(\d),(\d)/g, "$1.$2")) } catch (e3) { return { error: v } }
            }
        }
        return v
    }

    function rawCall(m, a) {
        if (typeof logos === "undefined" || !logos.callModule) return "NO BRIDGE (logos object missing)"
        try { return String(logos.callModule("hushrate_core", m, a)) } catch (e) { return "EXCEPTION: " + e }
    }
    function selfTest() {
        var out = [], ms = ["ping", "hwInfo", "sysStats", "binStatus", "mineState", "nodeScanState", "jobState"]
        for (var i = 0; i < ms.length; ++i) {
            var r = rawCall(ms[i], [])
            out.push(ms[i] + " => " + (r.length > 320 ? r.slice(0, 320) + "…" : r))
        }
        out.push("lang=" + lang + " langs=" + langs.length + " sample=" + tr("start") + " | " + tr("w_title"))
        out.push("hwOk=" + hwOk + " hwErr=" + hwErr + " sysErr=" + sysErr)
        selfOut.text = out.join("\n")
    }
    function loadHw() {
        var h = rpc("hwInfo", [])
        if (h.error || !h.logical) {
            hwOk = false
            hwErr = h.error ? String(h.error) : "unexpected reply"
            if (!hwTried) {
                threadSlider.to = 32
                threadSlider.value = savedThreads > 0 ? Math.min(savedThreads, 32) : 2
            }
        } else {
            hw = h; hwOk = true; hwErr = ""
            threadSlider.to = Math.max(1, h.logical)
            threadSlider.value = savedThreads > 0 ? Math.min(savedThreads, threadSlider.to) : h.recommended
        }
        hwTried = true
    }
    function say(msg, bad) { notice = msg; noticeBad = !!bad }
    function copyText(s, what) {
        clip.text = s
        clip.selectAll(); clip.copy(); clip.deselect()
        say(tr("m_copied", what), false)
    }
    // The Basecamp plugin sandbox blocks https (and Qt.openUrlExternally), so links are shown and copied instead.
    property string linkTitle: ""
    property string linkUrl: ""
    function showLink(title, u) {
        if (!u) return
        linkTitle = title; linkUrl = u
        copyText(u, tr("m_link"))
        linkPopup.open()
    }

    // ---------------------------------------------------------- config
    function cfgText() {
        return "wallet=" + walletField.text.trim() +
               "\nworker=" + workerField.text.trim() +
               "\nthreads=" + threadSlider.value +
               "\nnodeType=" + nodeType +
               "\nnodes=" + nodesField.text.trim().replace(/\n+/g, ",") +
               "\nsidechain=" + sidechain +
               "\nlight=" + (lightMode ? "1" : "0") +
               "\nlang=" + lang +
               "\nmaxtemp=" + (tempLimitOn ? tempLimit : 0) +
               "\nsilent=" + (silentMode ? "1" : "0") +
               "\npausebat=" + (pauseBattery ? "1" : "0") +
               "\nfallback=" + (fallbackPublic ? "1" : "0") +
               "\nupdSkip=" + updSkip +
               "\nbinDir=" + binDir + "\n"
    }
    function saveCfg() {
        if (!loaded) return
        var r = rpc("cfgSave", [cfgText()])
        if (r.error) say(tr("m_saveErr", r.error), true)
    }
    function loadCfg() {
        var t = rpc("cfgLoad", [])
        var kv = {}
        if (typeof t === "string") {
            t.split("\n").forEach(function (l) {
                var i = l.indexOf("=")
                if (i > 0) kv[l.slice(0, i).trim()] = l.slice(i + 1).trim()
            })
        }
        if (kv.wallet) walletField.text = kv.wallet
        if (kv.worker) workerField.text = kv.worker
        if (kv.nodeType === "manual") nodeType = "manual"
        nodesField.text = kv.nodes ? kv.nodes.split(",").join("\n") : ""
        if (kv.sidechain) sidechain = kv.sidechain
        lightMode = kv.light === "1"
        var sup = langs
        var sys = Qt.locale().name ? String(Qt.locale().name).slice(0, 2).toLowerCase() : "en"
        lang = kv.lang && sup.indexOf(kv.lang) >= 0 ? kv.lang : (sup.indexOf(sys) >= 0 ? sys : "en")
        if (kv.maxtemp !== undefined) { var mt = parseInt(kv.maxtemp); tempLimitOn = mt > 0; if (mt > 0) tempLimit = mt }
        silentMode = kv.silent === "1"
        pauseBattery = kv.pausebat === "1"
        fallbackPublic = kv.fallback !== "0"
        updSkip = kv.updSkip || ""
        if (kv.binDir) { binDir = kv.binDir; rpc("setBinDir", [binDir]) }
        savedThreads = kv.threads ? parseInt(kv.threads) : 0
        loadHw()
        loaded = true
    }

    // ---------------------------------------------------------- actions
    function refreshBins() {
        var r = rpc("binStatus", [])
        if (r.error) { say(r.error, true); return }
        bins = r
    }
    function binLine(name) {
        var b = bins[name]
        if (!b) return name + ": ?"
        if (!b.present) return tr("b_notinst", name)
        return name + ": " + (b.version || "present") + "  " + (b.verified ? tr("b_ver") + " " + b.tag : tr("b_notver"))
    }
    function install(which) {
        var r = rpc("binInstall", [which])
        if (r.error) { say(r.error, true); return }
        busyInstall = true
        say(tr("m_dl"), false)
    }
    function trust(which) {
        var r = rpc("binTrust", [which])
        if (r.error) say(r.error, true); else say(tr("m_trusted", which), false)
        refreshBins()
    }
    function scanNodes() {
        var r = rpc("nodeScan", [nodesCfg()])
        if (r.error) say(r.error, true)
    }
    function nodesCfg() {
        return "nodes=" + (nodeType === "manual" ? nodesField.text.trim().replace(/\n+/g, ",") : defaultNodes.replace(/\n/g, ",")) +
               "\ndynamic=" + (nodeType === "public" || fallbackPublic ? "1" : "0")
    }
    function startMining() {
        if (!walletValid) { say(tr("m_badWallet"), true); return }
        if (!binsReady) { say(tr("m_noBins"), true); return }
        if (nodeType === "manual" && nodesField.text.trim() === "" && !fallbackPublic) { say(tr("n_manualEmpty"), true); return }
        saveCfg()
        var cfg = "wallet=" + walletField.text.trim() +
                  "\nthreads=" + threadSlider.value +
                  "\n" + nodesCfg() +
                  "\nsidechain=" + sidechain +
                  "\nlight=" + (lightMode ? "1" : "0") +
                  "\nmaxtemp=" + (tempLimitOn ? tempLimit : 0) +
                  "\nsilent=" + (silentMode ? "1" : "0") +
                  "\npausebat=" + (pauseBattery ? "1" : "0")
        var r = rpc("mineStart", [cfg])
        if (r.error) { say(r.error, true); return }
        say("", false)
        logNext = 0
        logModel.clear()
        history = []
        zeroTicks = 0
        chartCanvas.requestPaint()
    }
    function stopMining() {
        var r = rpc("mineStop", [])
        say(r.error ? r.error : tr("m_stopping"), !!r.error)
    }

    function checkUpdates() { var r = rpc("updateCheck", []); if (r.error) say(String(r.error), true) }
    function doUpdate() {
        if (running) { say(tr("u_stopFirst"), true); return }
        install("all")
    }
    function laterUpdate() { updSkip = updText; updHidden = true; saveCfg() }
    function copyHuge() {
        copyText(hugeCmds(), tr("h_copied"))
    }
    function hugeCmds() {
        var n = hw.hugeWant || 1280
        return "sudo sysctl -w vm.nr_hugepages=" + n + "\necho 'vm.nr_hugepages=" + n + "' | sudo tee /etc/sysctl.d/99-hushrate.conf"
    }
    function addLocalNode() {
        var cur = nodesField.text.trim()
        if (cur.indexOf("127.0.0.1:18081") >= 0) return
        nodesField.text = "127.0.0.1:18081:18083" + (cur ? "\n" + cur : "")
        scanNodes()
    }
    function addLog(lines) {
        if (!lines || lines.length === 0) return
        for (var i = 0; i < lines.length; ++i) logModel.append({ t: lines[i] })
        while (logModel.count > 800) logModel.remove(0, 200)
        if (logView.follow) logView.positionViewAtEnd()
    }
    function copyLog() {
        var s = []
        for (var i = 0; i < logModel.count; ++i) s.push(logModel.get(i).t)
        copyText(s.join("\n"), "Log")
    }

    function tick() {
        tickN++
        var s = rpc("mineState", [])
        if (!s.error) {
            st = s; running = s.running === true
            if (running && s.phase === "mining") zeroTicks = (parseFloat(s.hashrate) || 0) > 0 ? 0 : zeroTicks + 1
            else zeroTicks = 0
            if (running && s.hashrate) {
                var h = history.slice(-119); h.push(parseFloat(s.hashrate) || 0); history = h
                chartCanvas.requestPaint()
            }
        }
        var l = rpc("mineLog", [String(logNext)])
        if (!l.error && l.lines) { logNext = l.next; addLog(l.lines) }
        if (tickN % 2 === 0) {
            var y = rpc("sysStats", [])
            if (y.error) sysErr = String(y.error); else { sys = y; sysErr = "" }
        }
        if (!hwOk && tickN % 5 === 0) loadHw()
        if (hwOk && tickN % 10 === 0) { var h2 = rpc("hwInfo", []); if (!h2.error && h2.logical) hw = h2 }
        if (tickN === 3 || tickN % 21600 === 0) checkUpdates()
        if (tickN === 6 || tickN % 5 === 0 && upd.running) { var u2 = rpc("updateState", []); if (!u2.error) upd = u2 }
        if (tickN % 60 === 0) { var u3 = rpc("updateState", []); if (!u3.error) upd = u3 }
        if (scan.running || tickN % 5 === 0) { var n = rpc("nodeScanState", []); if (!n.error) scan = n }
        if (busyInstall) {
            var j = rpc("jobState", [])
            if (!j.error) {
                jobLog.text = (j.log || []).join("\n")
                if (!j.running) {
                    busyInstall = false
                    say(j.ok ? tr("m_installed") : tr("m_installErr"), !j.ok)
                    refreshBins()
                    if (j.ok) { updHidden = false; updSkip = ""; checkUpdates() }
                }
            }
        }
    }

    onLangChanged: if (typeof chartCanvas !== "undefined") chartCanvas.requestPaint()
    Timer { interval: 1000; repeat: true; running: true; onTriggered: root.tick() }
    Timer { id: saveTimer; interval: 800; onTriggered: root.saveCfg() }
    Component.onCompleted: { loadCfg(); refreshBins(); tick(); if (nodeType === "public" || nodesField.text) scanNodes() }

    ListModel { id: logModel }
    TextEdit { id: clip; visible: false }

    // ---------------------------------------------------------- reusable pieces
    component Cap: Text {
        color: root.chartDim; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5; font.family: root.mono
    }
    component Card: Rectangle {
        default property alias content: inner.data
        property string title: ""
        color: root.panel; radius: 8; border.color: root.chartDim; border.width: 1
        implicitHeight: inner.implicitHeight + 28
        ColumnLayout {
            id: inner
            anchors.fill: parent; anchors.margins: 14
            spacing: 8
            Cap { text: title; visible: title !== "" }
        }
    }
    component HBtn: Rectangle {
        id: b
        property string label: ""
        property bool primary: false
        property bool danger: false
        property bool enabledBtn: true
        signal clicked()
        implicitWidth: lab.implicitWidth + 28; implicitHeight: 34; radius: 6
        color: !enabledBtn ? (primary ? root.chartDim : "transparent")
               : primary ? (ma.containsMouse ? "#d9ff66" : root.chart) : (ma.containsMouse ? root.raised : "transparent")
        border.width: primary ? 0 : 1
        border.color: danger ? root.danger : (ma.containsMouse && enabledBtn ? root.chart : root.chartDim)
        opacity: enabledBtn ? 1 : 0.55
        Text {
            id: lab; anchors.centerIn: parent; text: b.label
            width: Math.min(implicitWidth, b.width - 12)
            fontSizeMode: Text.HorizontalFit; minimumPixelSize: 8; elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            font.family: root.mono; font.pixelSize: 12; font.bold: true; font.letterSpacing: 1
            color: b.primary ? "#0a0b08" : (b.danger ? root.danger : (ma.containsMouse && b.enabledBtn ? root.paper : root.chart))
        }
        MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; enabled: b.enabledBtn; onClicked: b.clicked() }
    }
    // Vector icon: data = { vw, vh, shapes: [ { c: "#rrggbb", d: "M x y L x y C x1 y1 x2 y2 x y Z" } ] } (absolute, 0..1000)
    component VecIcon: Canvas {
        property var spec: null
        onSpecChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            var c = getContext("2d"); c.reset()
            if (!spec || !spec.shapes) return
            var s = Math.min(width / spec.vw, height / spec.vh)
            var ox = (width - spec.vw * s) / 2, oy = (height - spec.vh * s) / 2
            for (var i = 0; i < spec.shapes.length; ++i) {
                var t = spec.shapes[i].d.match(/[MLCZ]|-?\d+(?:\.\d+)?/g)
                c.beginPath()
                var k = 0
                while (k < t.length) {
                    var op = t[k++]
                    if (op === "M") { c.moveTo(ox + t[k] * s, oy + t[k + 1] * s); k += 2 }
                    else if (op === "L") { c.lineTo(ox + t[k] * s, oy + t[k + 1] * s); k += 2 }
                    else if (op === "C") { c.bezierCurveTo(ox + t[k] * s, oy + t[k + 1] * s, ox + t[k + 2] * s, oy + t[k + 3] * s, ox + t[k + 4] * s, oy + t[k + 5] * s); k += 6 }
                    else if (op === "Z") c.closePath()
                }
                c.fillStyle = spec.shapes[i].c
                c.fill()
            }
        }
    }
    // Round icon button used in the footer (GitHub, X)
    component IconBtn: Rectangle {
        id: ib
        property var icon: null
        property string tip: ""
        signal clicked()
        implicitWidth: 30; implicitHeight: 30; radius: 15
        color: iba.containsMouse ? root.raised : "transparent"
        border.width: 1; border.color: iba.containsMouse ? root.chart : root.chartDim
        VecIcon { anchors.centerIn: parent; width: 18; height: 18; spec: ib.icon }
        MouseArea { id: iba; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: ib.clicked() }
        ToolTip.visible: iba.containsMouse && ib.tip !== ""
        ToolTip.text: ib.tip
        ToolTip.delay: 300
    }
    component Field: TextField {
        color: root.paper; placeholderTextColor: root.chartDim
        font.family: root.mono; font.pixelSize: 13; selectByMouse: true
        background: Rectangle { color: "#0d0e09"; radius: 6; border.width: 1; border.color: parent.activeFocus ? root.chart : root.chartDim }
    }
    component Stat: Rectangle {
        property string label: ""
        property string value: "—"
        property string unit: ""
        property color valueColor: root.chart
        color: root.panel; radius: 8; border.color: root.chartDim; border.width: 1
        implicitHeight: 78
        Column {
            anchors.fill: parent; anchors.margins: 12; spacing: 6
            Cap { text: label }
            Row {
                spacing: 6
                Text { text: value; color: valueColor; font.family: root.mono; font.pixelSize: 24; font.bold: true }
                Text { text: unit; color: root.ash; font.family: root.mono; font.pixelSize: 12; anchors.baseline: parent.children[0].baseline; visible: unit !== "" }
            }
        }
    }

    // ---------------------------------------------------------- page
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#1c2406" }
            GradientStop { position: 0.35; color: root.void_ }
            GradientStop { position: 1.0; color: root.void_ }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // ------------------------------------------------------ header
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Rectangle {
                width: 44; height: 44; radius: 10; color: root.void_; border.color: root.chart; border.width: 2
                VecIcon { anchors.centerIn: parent; width: 28; height: 28; spec: root.icoLogo }
            }
            ColumnLayout {
                spacing: 0
                Text { text: "HUSHRATE MINER"; color: root.chart; font.family: root.mono; font.pixelSize: 22; font.bold: true; font.letterSpacing: 3 }
                Text { text: "fast · private · quiet"; color: root.ash; font.family: root.mono; font.pixelSize: 11; font.letterSpacing: 2 }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                implicitWidth: 74; implicitHeight: 26; radius: 6
                color: lma.containsMouse ? root.raised : "transparent"; border.width: 1; border.color: lma.containsMouse ? root.chart : root.chartDim
                Text { anchors.centerIn: parent; text: "🌐 " + root.lang.toUpperCase(); font.family: root.mono; font.pixelSize: 11; font.bold: true; color: root.chart }
                MouseArea { id: lma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: langPopup.open() }
            }
            Rectangle {
                implicitWidth: badgeText.implicitWidth + 20; implicitHeight: 26; radius: 13
                color: "transparent"; border.color: root.chartDim
                Text { id: badgeText; anchors.centerIn: parent; text: root.tr("badge"); color: root.ash; font.family: root.mono; font.pixelSize: 11 }
            }
        }

        // ------------------------------------------------------ banners (updates / watchdog)
        Rectangle {
            Layout.fillWidth: true
            visible: !root.binsReady && !root.running
            implicitHeight: binRow.implicitHeight + 16; radius: 8; color: "#1f2108"; border.color: "#e6c84a"
            RowLayout {
                id: binRow
                anchors.fill: parent; anchors.margins: 8; spacing: 8
                Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: root.busyInstall ? root.tr("m_dl") : root.tr("bins_banner"); color: "#e6c84a"; font.family: root.mono; font.pixelSize: 12 }
                HBtn { label: root.tr("bins_btn"); primary: true; enabledBtn: !root.busyInstall; onClicked: root.install("all") }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            visible: root.updShow
            implicitHeight: updRow.implicitHeight + 16; radius: 8; color: "#1f2108"; border.color: "#e6c84a"
            RowLayout {
                id: updRow
                anchors.fill: parent; anchors.margins: 8; spacing: 8
                Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: root.tr("u_banner", root.updText); color: "#e6c84a"; font.family: root.mono; font.pixelSize: 12 }
                HBtn { label: root.tr("u_update"); primary: true; enabledBtn: !root.busyInstall; onClicked: root.doUpdate() }
                HBtn { label: root.tr("u_later"); onClicked: root.laterUpdate() }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            readonly property string msg: root.phase === "paused" ? root.tr("wd_paused", root.st.pauseReason)
                                          : root.phase === "error" ? root.tr("wd_error", root.st.error)
                                          : (root.running && root.zeroTicks >= 30 ? root.tr("wd_zero") : "")
            visible: msg !== ""
            implicitHeight: wdText.implicitHeight + 16; radius: 8; color: "#1f0d0d"
            border.color: root.phase === "paused" ? "#e6c84a" : root.danger
            Text { id: wdText; anchors.fill: parent; anchors.margins: 8; wrapMode: Text.Wrap; text: parent.msg
                   color: root.phase === "paused" ? "#e6c84a" : root.danger; font.family: root.mono; font.pixelSize: 12 }
        }

        // ------------------------------------------------------ body
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            // ---------------- left column: controls
            ScrollView {
                Layout.preferredWidth: 400
                Layout.minimumWidth: 340
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: parent.width
                    spacing: 10

                    Card {
                        Layout.fillWidth: true
                        title: root.tr("w_title")
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Field {
                                id: walletField
                                Layout.fillWidth: true
                                echoMode: root.showWallet ? TextInput.Normal : TextInput.Password
                                placeholderText: root.tr("w_ph")
                                onTextChanged: saveTimer.restart()
                            }
                            HBtn { label: root.showWallet ? root.tr("hide") : root.tr("show"); onClicked: root.showWallet = !root.showWallet }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: walletField.text.length > 0
                            text: root.walletValid ? root.tr("w_ok") : root.tr("w_bad", walletField.text.trim().length)
                            color: root.walletValid ? root.chart : root.danger
                            font.family: root.mono; font.pixelSize: 11
                        }
                        Field {
                            id: workerField
                            Layout.fillWidth: true
                            placeholderText: root.tr("worker_ph")
                            onTextChanged: saveTimer.restart()
                        }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.Wrap
                            text: root.tr("pool_info", root.sidechain)
                            color: root.ash; font.family: root.mono; font.pixelSize: 11
                        }
                    }

                    Card {
                        Layout.fillWidth: true
                        title: root.tr("n_title")
                        RowLayout {
                            spacing: 8
                            HBtn { label: root.tr("n_public"); primary: root.nodeType === "public"; onClicked: { root.nodeType = "public"; saveTimer.restart(); root.scanNodes() } }
                            HBtn { label: root.tr("n_manual"); primary: root.nodeType === "manual"; onClicked: { root.nodeType = "manual"; saveTimer.restart(); root.scanNodes() } }
                        }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.Wrap
                            color: root.paper; font.family: root.mono; font.pixelSize: 12
                            text: root.running && root.st.node ? root.tr("n_using", root.st.node)
                                  : root.scan.running ? root.tr("n_scanning")
                                  : root.scan.valid && root.scan.count > 0 ? root.tr("n_best", root.scan.best.host + ":" + root.scan.best.rpc, root.scan.best.ms, root.scan.count)
                                  : root.scan.valid ? root.tr("n_none") : root.tr("n_notscanned")
                        }
                        TextArea {
                            id: nodesField
                            visible: root.nodeType === "manual"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 84
                            wrapMode: TextArea.Wrap
                            color: root.paper; font.family: root.mono; font.pixelSize: 12
                            placeholderText: root.tr("n_ph")
                            placeholderTextColor: root.chartDim
                            background: Rectangle { color: "#0d0e09"; radius: 6; border.color: root.chartDim }
                            onTextChanged: saveTimer.restart()
                        }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.Wrap
                            visible: root.nodeType === "manual"
                            text: root.tr("n_hint")
                            color: root.ash; font.family: root.mono; font.pixelSize: 11
                        }
                        CheckBox {
                            visible: root.nodeType === "manual"
                            text: root.tr("n_fallback")
                            checked: root.fallbackPublic
                            onToggled: { root.fallbackPublic = checked; saveTimer.restart() }
                            palette.windowText: root.paper
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 8
                            visible: root.nodeType === "manual"
                            HBtn { label: root.tr("n_local"); onClicked: root.addLocalNode() }
                            HBtn { label: root.tr("n_flags"); onClicked: root.copyText("--zmq-pub tcp://127.0.0.1:18083 --rpc-bind-ip 127.0.0.1 --rpc-bind-port 18081", root.tr("n_flagsName")) }
                        }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.Wrap
                            visible: root.nodeType === "manual" && root.scan.valid && !root.scan.running
                            text: root.tr("n_stats", root.scan.probed || 0, root.scan.rpcFail || 0, root.scan.zmqFail || 0)
                            color: root.ash; font.family: root.mono; font.pixelSize: 11
                        }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.Wrap
                            visible: root.nodeType === "manual" && root.scan.valid && root.scan.count > 0 && !root.scan.running
                            color: root.paper; font.family: root.mono; font.pixelSize: 11
                            text: {
                                var a = [root.tr("n_listTitle")], n = root.scan.nodes || []
                                for (var i = 0; i < n.length; ++i) a.push("  " + n[i].host + ":" + n[i].rpc + " · zmq " + n[i].zmq + " · " + n[i].ms + " ms")
                                return a.join("\n")
                            }
                        }
                        HBtn { label: root.tr("n_refresh"); enabledBtn: !root.running && !root.scan.running; onClicked: root.scanNodes() }
                    }

                    Card {
                        Layout.fillWidth: true
                        title: root.tr("p_title")
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Slider {
                                id: threadSlider
                                Layout.fillWidth: true
                                from: 1; to: 4; stepSize: 1; value: 3
                                onMoved: saveTimer.restart()
                                background: Rectangle {
                                    x: threadSlider.leftPadding; y: threadSlider.topPadding + threadSlider.availableHeight / 2 - 2
                                    width: threadSlider.availableWidth; height: 4; radius: 2; color: "#2a3012"
                                    Rectangle { width: threadSlider.visualPosition * parent.width; height: 4; radius: 2; color: root.chart }
                                }
                                handle: Rectangle {
                                    x: threadSlider.leftPadding + threadSlider.visualPosition * (threadSlider.availableWidth - width)
                                    y: threadSlider.topPadding + threadSlider.availableHeight / 2 - height / 2
                                    width: 16; height: 16; radius: 8; color: root.chart; border.color: root.void_; border.width: 2
                                }
                            }
                            Text { text: root.tr("p_threads", Math.round(threadSlider.value)); color: root.chart; font.family: root.mono; font.pixelSize: 13; font.bold: true }
                        }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.Wrap
                            text: root.hwOk ? root.tr("p_detected", root.hw.cores, root.hw.logical, (root.hw.ramGb > 0 ? root.tr("p_ram", root.hw.ramGb) : ""), root.hw.recommended)
                                  : root.tr("p_hwfail", root.hwErr)
                            color: root.hwOk ? root.ash : root.danger; font.family: root.mono; font.pixelSize: 11
                        }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.Wrap
                            visible: root.hwOk
                            text: root.hw.hugePages ? root.tr("p_hugeOn", root.hw.hugeNow) : root.tr("p_hugeOff")
                            color: root.hw.hugePages ? root.ash : "#e6c84a"; font.family: root.mono; font.pixelSize: 11
                        }
                        HBtn { visible: root.hwOk && !root.hw.hugePages; label: root.tr("p_hugeBtn"); onClicked: hugePopup.open() }
                        HBtn { label: root.tr("p_apply"); enabledBtn: true; onClicked: { root.saveCfg(); root.say(root.running ? root.tr("p_savedNext") : root.tr("p_saved"), false) } }
                    }

                    Card {
                        Layout.fillWidth: true
                        title: root.tr("s_title")
                        RowLayout {
                            spacing: 8
                            Rectangle { width: 12; height: 12; radius: 6; color: root.phaseColor }
                            Text { text: root.tr("ph_" + root.phase); color: root.phaseColor; font.family: root.mono; font.pixelSize: 14; font.bold: true; font.letterSpacing: 2 }
                        }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.Wrap
                            visible: root.st.error !== ""
                            text: root.st.error; color: root.danger; font.family: root.mono; font.pixelSize: 11
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            HBtn { Layout.fillWidth: true; Layout.preferredWidth: 130; label: root.tr("start"); primary: true; enabledBtn: !root.running && !root.busyInstall; onClicked: root.startMining() }
                            HBtn { Layout.fillWidth: true; Layout.preferredWidth: 70; label: root.tr("stop"); danger: true; enabledBtn: root.running; onClicked: root.stopMining() }
                            HBtn { Layout.fillWidth: true; Layout.preferredWidth: 130; label: root.tr("adv"); onClicked: { root.refreshBins(); advanced.open() } }
                        }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.Wrap
                            visible: root.notice !== ""
                            text: root.notice; color: root.noticeBad ? root.danger : root.chart
                            font.family: root.mono; font.pixelSize: 11
                        }
                    }
                }
            }

            // ---------------- right column: stats
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10

                GridLayout {
                    Layout.fillWidth: true
                    columns: 3
                    columnSpacing: 10; rowSpacing: 10
                    Stat { Layout.fillWidth: true; label: root.tr("st_hash"); value: root.st.hashrate ? root.st.hashrate : "—"; unit: root.st.hashrate ? "H/s" : "" }
                    Stat { Layout.fillWidth: true; label: root.tr("st_h60"); value: root.st.h60 ? root.st.h60 : "—"; unit: root.st.h60 ? "H/s" : "" }
                    Stat { Layout.fillWidth: true; label: root.tr("st_h15"); value: root.st.h15 ? root.st.h15 : "—"; unit: root.st.h15 ? "H/s" : "" }
                    Stat { Layout.fillWidth: true; label: root.tr("st_up"); value: root.running && root.st.uptime > 0 ? root.fmtUp(root.st.uptime) : "—" }
                    Stat { Layout.fillWidth: true; label: root.tr("st_acc"); value: root.st.shares ? root.st.shares : "—" }
                    Stat { Layout.fillWidth: true; label: root.tr("st_found"); value: String(root.st.p2poolShares || 0) }
                    Stat { Layout.fillWidth: true; label: "CPU"; value: root.sys.cpu >= 0 ? Math.round(root.sys.cpu) : "—"; unit: root.sys.cpu >= 0 ? "%" : "" }
                    Stat { Layout.fillWidth: true; label: "RAM"; value: root.sys.ram >= 0 ? Math.round(root.sys.ram) : "—"; unit: root.sys.ram >= 0 ? "%" : "" }
                    Stat { Layout.fillWidth: true; label: "TEMP"; value: root.sys.temp >= 0 ? Math.round(root.sys.temp) : "—"; unit: root.sys.temp >= 0 ? "°C" : ""
                           valueColor: root.sys.temp >= 85 ? root.danger : root.chart }
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    visible: root.sysErr !== ""
                    text: root.tr("sysErr", root.sysErr)
                    color: root.danger; font.family: root.mono; font.pixelSize: 11
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    visible: root.sysErr === "" && root.sys.temp < 0 && root.tickN > 6
                    text: root.tr("noTemp")
                    color: root.ash; font.family: root.mono; font.pixelSize: 11
                }

                Card {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 210
                    implicitHeight: 210
                    title: root.tr("hist_title")
                    Text {
                        Layout.fillWidth: true
                        color: root.paper; font.family: root.mono; font.pixelSize: 12
                        text: root.history.length ? root.tr("hist_text", Math.round(root.histCur), Math.round(root.histAvg), Math.round(root.histPeak), root.history.length)
                                                  : root.tr("hist_none")
                    }
                    Canvas {
                        id: chartCanvas
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        onPaint: {
                            var c = getContext("2d"); c.reset()
                            var d = root.history
                            var x0 = 56, y0 = 8, pw = width - x0 - 8, ph = height - y0 - 18
                            c.font = "10px monospace"; c.fillStyle = "#8a9270"; c.strokeStyle = "#2a3012"; c.lineWidth = 1
                            var mx = 1
                            for (var i = 0; i < d.length; ++i) mx = Math.max(mx, d[i])
                            mx *= 1.15
                            for (var g = 0; g < 4; ++g) {
                                var gy = Math.round(y0 + ph * g / 3) + 0.5
                                c.beginPath(); c.moveTo(x0, gy); c.lineTo(x0 + pw, gy); c.stroke()
                                var val = d.length ? Math.round(mx * (1 - g / 3)) : (g === 3 ? 0 : "")
                                c.fillText(String(val), 2, gy + 3)
                            }
                            if (d.length < 2) {
                                c.fillStyle = "#5c6b26"; c.font = "12px monospace"
                                c.fillText(root.tr("hist_wait"), x0 + 10, y0 + ph / 2)
                                return
                            }
                            c.beginPath()
                            for (var k = 0; k < d.length; ++k) {
                                var px = x0 + pw * k / (d.length - 1), py = y0 + ph - ph * d[k] / mx
                                if (k === 0) c.moveTo(px, py); else c.lineTo(px, py)
                            }
                            c.strokeStyle = "#ccff33"; c.lineWidth = 2; c.stroke()
                            c.lineTo(x0 + pw, y0 + ph); c.lineTo(x0, y0 + ph); c.closePath()
                            c.fillStyle = "rgba(204,255,51,0.10)"; c.fill()
                            c.fillStyle = "#8a9270"; c.font = "10px monospace"
                            c.fillText("-" + d.length + " s", x0, height - 4)
                            c.fillText(root.tr("hist_now"), x0 + pw - 6 * root.tr("hist_now").length, height - 4)
                            c.fillText("H/s", 2, height - 4)
                        }
                    }
                }

                Card {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 160
                    title: root.tr("log_title")
                    RowLayout {
                        spacing: 8
                        HBtn { label: root.tr("copy"); onClicked: root.copyLog() }
                        HBtn { label: root.tr("clear"); onClicked: logModel.clear() }
                        Text { text: logView.follow ? root.tr("follow_on") : root.tr("follow_off"); color: root.ash; font.family: root.mono; font.pixelSize: 11 }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "#0d0e09"; radius: 6; border.color: "#2a3012"
                        ListView {
                            id: logView
                            anchors.fill: parent; anchors.margins: 6
                            clip: true
                            model: logModel
                            property bool follow: true
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOn }
                            onMovementEnded: follow = atYEnd
                            onFlickEnded: follow = atYEnd
                            delegate: Text {
                                width: logView.width - 14
                                text: t
                                wrapMode: Text.WrapAnywhere
                                color: t.indexOf("ERROR") >= 0 || t.indexOf("FAILED") >= 0 ? root.danger : (t.indexOf("[core]") === 0 ? root.chart : root.paper)
                                font.family: root.mono; font.pixelSize: 11
                            }
                            onCountChanged: if (follow) Qt.callLater(positionViewAtEnd)
                        }
                        MouseArea {
                            anchors.fill: parent; acceptedButtons: Qt.NoButton
                            onWheel: function (w) { logView.flick(0, w.angleDelta.y * 6); logView.follow = false; w.accepted = true }
                        }
                    }
                }
            }
        }

        // ------------------------------------------------------ footer
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text { text: root.tr("foot_ver", root.appVersion); color: root.chartDim; font.family: root.mono; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            Text { text: root.tr("foot_dev"); color: root.chartDim; font.family: root.mono; font.pixelSize: 11 }
            IconBtn { visible: root.urlGithub !== ""; icon: root.icoGithub; tip: "GitHub"; onClicked: root.showLink("GitHub", root.urlGithub) }
            IconBtn { visible: root.urlX !== ""; icon: root.icoX; tip: "X"; onClicked: root.showLink("X", root.urlX) }
        }
    }

    // ---------------------------------------------------------- link popup
    Popup {
        id: linkPopup
        modal: true
        anchors.centerIn: parent
        width: Math.min(root.width - 40, 560)
        padding: 16
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: root.panel; radius: 10; border.color: root.chart; border.width: 1 }
        ColumnLayout {
            width: parent.width
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Cap { text: root.linkTitle.toUpperCase(); Layout.fillWidth: true }
                HBtn { label: root.tr("close"); onClicked: linkPopup.close() }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Field { Layout.fillWidth: true; readOnly: true; text: root.linkUrl }
                HBtn { label: root.tr("copy"); onClicked: root.copyText(root.linkUrl, root.tr("m_link")) }
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.Wrap
                visible: root.notice !== ""
                text: root.notice; color: root.noticeBad ? root.danger : root.chart
                font.family: root.mono; font.pixelSize: 11
            }
        }
    }

    // ---------------------------------------------------------- language popup
    Popup {
        id: langPopup
        objectName: "langPopup"
        modal: true
        anchors.centerIn: parent
        width: Math.min(root.width - 40, 520)
        padding: 16
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: root.panel; radius: 10; border.color: root.chart; border.width: 1 }
        ColumnLayout {
            width: parent.width
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Cap { text: root.tr("lang_title"); Layout.fillWidth: true }
                HBtn { label: root.tr("close"); onClicked: langPopup.close() }
            }
            Flow {
                Layout.fillWidth: true; spacing: 8
                Repeater {
                    model: root.langs
                    HBtn { label: root.langLabel(modelData); primary: root.lang === modelData; onClicked: { root.lang = modelData; saveTimer.restart(); langPopup.close() } }
                }
            }
        }
    }

    // ---------------------------------------------------------- huge pages popup
    Popup {
        id: hugePopup
        objectName: "hugePopup"
        modal: true
        anchors.centerIn: parent
        width: Math.min(root.width - 40, 620)
        padding: 16
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: root.panel; radius: 10; border.color: root.chart; border.width: 1 }
        ColumnLayout {
            width: parent.width
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Cap { text: root.tr("h_title"); Layout.fillWidth: true }
                HBtn { label: root.tr("close"); onClicked: hugePopup.close() }
            }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: root.tr("h_intro"); color: root.paper; font.family: root.mono; font.pixelSize: 12 }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: root.tr("h_status", root.hw.hugeNow || 0, root.hw.hugeWant || 1280); color: root.chart; font.family: root.mono; font.pixelSize: 12 }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: root.tr("h_cmdTitle"); color: root.ash; font.family: root.mono; font.pixelSize: 11 }
            TextArea {
                Layout.fillWidth: true
                Layout.preferredHeight: 70
                readOnly: true; selectByMouse: true
                wrapMode: TextArea.WrapAnywhere
                text: root.hugeCmds()
                color: root.paper; font.family: root.mono; font.pixelSize: 12
                background: Rectangle { color: "#0d0e09"; radius: 6; border.color: root.chartDim }
            }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: root.tr("h_persist"); color: root.ash; font.family: root.mono; font.pixelSize: 11 }
            HBtn { label: root.tr("h_copy"); primary: true; onClicked: root.copyHuge() }
            Text {
                Layout.fillWidth: true; wrapMode: Text.Wrap
                visible: root.notice !== ""
                text: root.notice; color: root.noticeBad ? root.danger : root.chart
                font.family: root.mono; font.pixelSize: 11
            }
        }
    }

    // ---------------------------------------------------------- advanced popup
    Popup {
        id: advanced
        objectName: "advanced"
        modal: true
        anchors.centerIn: parent
        width: Math.min(root.width - 40, 640)
        height: Math.min(root.height - 40, 560)
        padding: 16
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: root.panel; radius: 10; border.color: root.chart; border.width: 1 }
        onClosed: root.saveCfg()
        ScrollView {
            anchors.fill: parent
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: parent.width
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    Cap { text: root.tr("adv_title"); Layout.fillWidth: true }
                    HBtn { label: root.tr("close"); onClicked: advanced.close() }
                }

                Cap { text: root.tr("bin_title") }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Field {
                        id: dirField; Layout.fillWidth: true
                        text: root.binDir
                        placeholderText: root.tr("bin_dir_ph")
                    }
                    HBtn {
                        label: root.tr("set")
                        onClicked: {
                            root.binDir = dirField.text.trim()
                            var r = root.rpc("setBinDir", [root.binDir])
                            root.say(r.error ? r.error : root.tr("m_binDirSet"), !!r.error)
                            root.saveCfg(); root.refreshBins()
                        }
                    }
                }
                Text { text: root.binLine("xmrig"); color: root.paper; font.family: root.mono; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.Wrap }
                Text { text: root.binLine("p2pool"); color: root.paper; font.family: root.mono; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.Wrap }
                Text {
                    visible: !!root.bins.tools && (!root.bins.tools.curl || !root.bins.tools.gpg || !root.bins.tools.tar)
                    text: root.tr("tools_missing", (root.bins.tools && !root.bins.tools.curl ? "curl " : "") +
                          (root.bins.tools && !root.bins.tools.gpg ? "gpg " : "") + (root.bins.tools && !root.bins.tools.tar ? "tar" : ""))
                    color: root.danger; font.family: root.mono; font.pixelSize: 12
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    HBtn { label: root.tr("b_dl"); primary: true; enabledBtn: !root.busyInstall && !root.running; onClicked: root.install("all") }
                    HBtn { label: root.tr("b_recheck"); onClicked: root.refreshBins() }
                    HBtn { label: root.tr("b_trustx"); enabledBtn: !root.running; onClicked: root.trust("xmrig") }
                    HBtn { label: root.tr("b_trustp"); enabledBtn: !root.running; onClicked: root.trust("p2pool") }
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    text: root.tr("b_note")
                    color: root.ash; font.family: root.mono; font.pixelSize: 11
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    HBtn { label: root.tr("u_check"); enabledBtn: !root.upd.running; onClicked: root.checkUpdates() }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.Wrap
                        text: root.upd.running ? root.tr("u_checking") : (root.updText !== "" ? root.tr("u_banner", root.updText) : (root.upd.done ? root.tr("u_none") : ""))
                        color: root.updText !== "" ? "#e6c84a" : root.ash; font.family: root.mono; font.pixelSize: 11
                    }
                }
                Text { id: jobLog; Layout.fillWidth: true; color: root.ash; wrapMode: Text.Wrap; font.family: root.mono; font.pixelSize: 11 }

                Cap { text: root.tr("sf_title") }
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    CheckBox {
                        text: root.tr("sf_temp", root.tempLimit)
                        checked: root.tempLimitOn
                        onToggled: { root.tempLimitOn = checked; saveTimer.restart() }
                        palette.windowText: root.paper
                    }
                }
                Slider {
                    Layout.fillWidth: true
                    from: 60; to: 95; stepSize: 1
                    enabled: root.tempLimitOn
                    opacity: root.tempLimitOn ? 1 : 0.4
                    value: root.tempLimit
                    onMoved: { root.tempLimit = Math.round(value); saveTimer.restart() }
                    background: Rectangle {
                        x: parent.leftPadding; y: parent.topPadding + parent.availableHeight / 2 - 2
                        width: parent.availableWidth; height: 4; radius: 2; color: "#2a3012"
                        Rectangle { width: parent.parent.visualPosition * parent.width; height: 4; radius: 2; color: root.chart }
                    }
                    handle: Rectangle {
                        x: parent.leftPadding + parent.visualPosition * (parent.availableWidth - width)
                        y: parent.topPadding + parent.availableHeight / 2 - height / 2
                        width: 16; height: 16; radius: 8; color: root.chart; border.color: root.void_; border.width: 2
                    }
                }
                Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: root.tr("sf_tempHelp"); color: root.ash; font.family: root.mono; font.pixelSize: 11 }
                CheckBox {
                    text: root.tr("sf_silent")
                    checked: root.silentMode
                    onToggled: { root.silentMode = checked; saveTimer.restart() }
                    palette.windowText: root.paper
                }
                CheckBox {
                    text: root.tr("sf_bat")
                    checked: root.pauseBattery
                    onToggled: { root.pauseBattery = checked; saveTimer.restart() }
                    palette.windowText: root.paper
                }
                Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: root.tr("sf_note"); color: root.ash; font.family: root.mono; font.pixelSize: 11 }

                Cap { text: root.tr("diag") }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    HBtn { label: root.tr("selftest"); onClicked: root.selfTest() }
                    HBtn { label: root.tr("copyres"); onClicked: root.copyText(selfOut.text, root.tr("diag")) }
                }
                TextArea {
                    id: selfOut
                    Layout.fillWidth: true
                    Layout.preferredHeight: 150
                    readOnly: true; selectByMouse: true
                    wrapMode: TextArea.WrapAnywhere
                    color: root.paper; font.family: root.mono; font.pixelSize: 11
                    placeholderText: root.tr("selftest_ph")
                    placeholderTextColor: root.chartDim
                    background: Rectangle { color: "#0d0e09"; radius: 6; border.color: root.chartDim }
                }

                Cap { text: root.tr("p2_title") }
                RowLayout {
                    spacing: 8
                    Text { text: root.tr("sidechain"); color: root.paper; font.family: root.mono; font.pixelSize: 12 }
                    ComboBox {
                        id: sideBox
                        model: ["main", "mini", "nano"]
                        currentIndex: Math.max(0, ["main", "mini", "nano"].indexOf(root.sidechain))
                        onActivated: { root.sidechain = currentText; saveTimer.restart() }
                    }
                }
                CheckBox {
                    text: root.tr("light")
                    checked: root.lightMode
                    onToggled: { root.lightMode = checked; saveTimer.restart() }
                    palette.windowText: root.paper
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    text: root.tr("side_help")
                    color: root.ash; font.family: root.mono; font.pixelSize: 11
                }
                Cap { text: root.tr("lang_title") }
                Flow {
                    Layout.fillWidth: true; spacing: 8
                    Repeater {
                        model: root.langs
                        HBtn { label: root.langLabel(modelData); primary: root.lang === modelData; onClicked: { root.lang = modelData; saveTimer.restart() } }
                    }
                }
            }
        }
    }
}
