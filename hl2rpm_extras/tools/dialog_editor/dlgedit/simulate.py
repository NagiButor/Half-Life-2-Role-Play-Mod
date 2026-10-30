"""Dialog play-through in the editor: the same rules as the game server
(dialog_definitions.cpp SendDialogNodeToPlayerEx, dialogsystem_server.cpp
HandleDialogChoice / ContinueDialog), quests and reputation as editable state."""
from .model import text_for

INT_MIN = -(2 ** 31)


class QuestState:
    def __init__(self, state="inactive", stage=0):
        self.state = state
        self.stage = stage


class Screen:
    def __init__(self):
        self.node_id = -1
        self.line = ""
        self.line_fallback = False
        self.voice = None          # Voice or None
        self.voice_lang = ""
        self.options = []          # [(index, text, fallback)]
        self.auto_next = -1
        self.auto_close = False
        self.pending_spoil = False
        self.kind = "node"         # node | returning | after
        self.events = []           # quest actions etc. that happened before this screen


class Simulator:
    def __init__(self, project, npc, lang):
        self.p = project
        self.npc = npc
        self.lang = lang
        self.spoken = False
        self.spoiled = False
        self.rep = 0
        self.quests = {}
        self.events = []
        self.current = None

    def quest(self, qid):
        if qid not in self.quests:
            self.quests[qid] = QuestState()
        return self.quests[qid]

    # --- helpers -------------------------------------------------------------------
    def _text(self, texts):
        return text_for(texts, self.lang, self.p.base_language)

    def _voice(self, voices):
        v = voices.get(self.lang)
        if v and not v.is_empty():
            return v, self.lang
        if self.p.settings.missing_voice == "base":
            b = voices.get(self.p.base_language)
            if b and not b.is_empty():
                return b, self.p.base_language
        return None, ""

    def _option_visible(self, o):
        if o.require_rep is not None and self.rep < o.require_rep:
            return False
        if o.require_stage and o.require_stage[0]:
            st = self.quests.get(o.require_stage[0])
            if not st or st.state != "active" or st.stage < o.require_stage[1]:
                return False
        if o.require_state and o.require_state[0]:
            st = self.quests.get(o.require_state[0])
            state = st.state if st else "inactive"
            if state != o.require_state[1]:
                return False
        if o.grant:
            st = self.quests.get(o.grant)
            if st and st.state in ("active", "completed"):
                return False
        return True

    def _send(self, node_id, override_line=None, override_voice=None, force_no_options=False,
              force_pending_spoil=False, forced_auto_next=INT_MIN, forced_auto_close=-1, kind="node"):
        npc = self.npc
        node = npc.node(node_id) or npc.node(npc.start_node) or (npc.nodes[0] if npc.nodes else None)
        if node is None:
            self.current = None
            return None
        sc = Screen()
        sc.kind = kind
        sc.node_id = node.id
        pending = force_pending_spoil or node.spoils
        sc.auto_next = forced_auto_next if forced_auto_next != INT_MIN else node.auto_next
        sc.auto_close = (forced_auto_close != 0) if forced_auto_close >= 0 else node.end
        suppress = force_no_options or pending
        if pending:
            sc.auto_next = -1
            sc.auto_close = True
        sc.pending_spoil = pending
        if override_line is not None:
            sc.line, sc.line_fallback = override_line
            sc.voice, sc.voice_lang = override_voice if override_voice else (None, "")
        else:
            sc.line, sc.line_fallback = self._text(node.line)
            sc.voice, sc.voice_lang = self._voice(node.voice)
        if not suppress:
            for i, o in enumerate(node.options):
                if self._option_visible(o):
                    t, fb = self._text(o.text)
                    sc.options.append((i, t, fb))
        sc.events = self.events
        self.events = []
        self.current = sc
        return sc

    # --- game entry points -----------------------------------------------------------
    def start(self):
        npc = self.npc
        if self.spoiled:
            self.current = None
            return None
        r = npc.returning
        if self.spoken and r.enabled():
            line = self._text(r.line)
            voice = self._voice(r.voice)
            if r.no_options:
                return self._send(npc.start_node, line, voice, True, False,
                                  r.next if r.next != -1 else INT_MIN, 1 if r.auto_close else 0, kind="returning")
            return self._send(npc.returning_target(), line, voice, False, False, -1, 0, kind="returning")
        return self._send(npc.start_node)

    def cont(self):
        """The line finished and there are no options."""
        sc = self.current
        if sc is None:
            return None
        if sc.pending_spoil:
            self.spoiled = True
            self.spoken = True
            self.events.append("NPC больше не разговаривает (spoils)")
            self.current = None
            return None
        if sc.auto_next != -1:
            return self._send(sc.auto_next)
        if sc.auto_close:
            self.spoken = True
            self.current = None
            return None
        return sc   # stuck: the game leaves the panel open

    def choose(self, index):
        sc = self.current
        if sc is None:
            return None
        node = self.npc.node(sc.node_id)
        if node is None or index < 0 or index >= len(node.options):
            return sc
        o = node.options[index]
        if not self._option_visible(o):
            return sc
        q = self.p.quest
        if o.grant:
            qd = q(o.grant)
            if qd and self.rep < qd.min_rep:
                self.events.append("квест «%s» не выдан: репутация %d < %d" % (o.grant, self.rep, qd.min_rep))
            else:
                st = self.quest(o.grant)
                st.state, st.stage = "active", 0
                self.events.append("выдан квест «%s»" % o.grant)
        if o.complete:
            st = self.quest(o.complete)
            if st.state != "active":
                self.events.append("CompleteQuest failed: квест «%s» не активен" % o.complete)
            else:
                st.state = "completed"
                qd = q(o.complete)
                rew = ", ".join("%s ×%d" % (a, b) for a, b in (qd.rewards if qd else []))
                self.events.append("квест «%s» выполнен%s" % (o.complete, (" — награда: " + rew) if rew else ""))
        if o.fail:
            st = self.quest(o.fail)
            if st.state != "active":
                self.events.append("FailQuest failed: квест «%s» не активен" % o.fail)
            else:
                st.state = "failed"
                self.events.append("квест «%s» провален" % o.fail)
        if o.set_stage and o.set_stage[0]:
            st = self.quest(o.set_stage[0])
            if st.state != "active":
                self.events.append("SetQuestStage failed: квест «%s» не активен" % o.set_stage[0])
            else:
                st.stage = o.set_stage[1]
                self.events.append("квест «%s»: этап %d" % (o.set_stage[0], o.set_stage[1]))
        if o.server_sequence:
            self.events.append("запуск на карте: %s → BeginSequence" % o.server_sequence)

        if o.next != -1:
            if o.spoils:
                return self._send(o.next, None, None, True, True, -1, 1)
            return self._send(o.next)
        if o.complete and o.after_next != -1:
            return self._send(o.after_next, None, None, True, False, -1, 1, kind="after")
        if o.complete and any(o.after_line.values()):
            return self._send(node.id, self._text(o.after_line), self._voice(o.after_voice), kind="after")
        self.spoken = True
        if o.spoils:
            return self._send(node.id, None, None, True, True, -1, 1)
        if o.close:
            self.current = None
            return None
        return sc
