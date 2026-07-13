let ws = null;
let username = "";

// 로비 화면들
const lobbyDiv = document.getElementById("lobby");
const createRoomDiv = document.getElementById("createRoom");
const passwordPromptDiv = document.getElementById("passwordPrompt");
const nicknameEntryDiv = document.getElementById("nicknameEntry");
const chatDiv = document.getElementById("chat");

const myRoomsTabBtn = document.getElementById("myRoomsTabBtn");
const findRoomsTabBtn = document.getElementById("findRoomsTabBtn");
const myRoomsPanel = document.getElementById("myRoomsPanel");
const findRoomsPanel = document.getElementById("findRoomsPanel");
const myRoomList = document.getElementById("myRoomList");

const roomSearchInput = document.getElementById("roomSearchInput");
const roomSearchBtn = document.getElementById("roomSearchBtn");
const roomSearchClearBtn = document.getElementById("roomSearchClearBtn");
const roomListDiv = document.getElementById("roomList");
const showCreateRoomBtn = document.getElementById("showCreateRoomBtn");

const newRoomName = document.getElementById("newRoomName");
const newRoomHasPassword = document.getElementById("newRoomHasPassword");
const newRoomPassword = document.getElementById("newRoomPassword");
const createRoomError = document.getElementById("createRoomError");
const createRoomBackBtn = document.getElementById("createRoomBackBtn");
const createRoomSubmitBtn = document.getElementById("createRoomSubmitBtn");

const passwordPromptRoomName = document.getElementById("passwordPromptRoomName");
const roomPasswordInput = document.getElementById("roomPasswordInput");
const passwordError = document.getElementById("passwordError");
const passwordBackBtn = document.getElementById("passwordBackBtn");
const passwordSubmitBtn = document.getElementById("passwordSubmitBtn");

const nicknameEntryRoomName = document.getElementById("nicknameEntryRoomName");
const roomUsernameInput = document.getElementById("roomUsernameInput");
const nicknameError = document.getElementById("nicknameError");
const nicknameBackBtn = document.getElementById("nicknameBackBtn");
const nicknameSubmitBtn = document.getElementById("nicknameSubmitBtn");

const chatRoomName = document.getElementById("chatRoomName");
const backToLobbyBtn = document.getElementById("backToLobbyBtn");
const leaveRoomBtn = document.getElementById("leaveRoomBtn");

// 채팅 화면 요소들
const logDiv = document.getElementById("log");
const logWrapperDiv = document.getElementById("logWrapper");
const dropOverlay = document.getElementById("dropOverlay");
const scrollToBottomBtn = document.getElementById("scrollToBottomBtn");
const msgInput = document.getElementById("msgInput");
const sendBtn = document.getElementById("sendBtn");
const emojiBtn = document.getElementById("emojiBtn");
const emojiPanel = document.getElementById("emojiPanel");
const paletteBtn = document.getElementById("paletteBtn");
const colorPalette = document.getElementById("colorPalette");
const mentionPopup = document.getElementById("mentionPopup");
const mentionToasts = document.getElementById("mentionToasts");
const replyPreview = document.getElementById("replyPreview");
const replyPreviewText = document.getElementById("replyPreviewText");
const replyPreviewCancel = document.getElementById("replyPreviewCancel");
const messageContextMenu = document.getElementById("messageContextMenu");
const ctxEditBtn = document.getElementById("ctxEditBtn");
const ctxPinBtn = document.getElementById("ctxPinBtn");
const ctxReactBtn = document.getElementById("ctxReactBtn");
const ctxDeleteBtn = document.getElementById("ctxDeleteBtn");
const reactionPicker = document.getElementById("reactionPicker");
const pinnedPopup = document.getElementById("pinnedPopup");
const pinnedPopupSender = document.getElementById("pinnedPopupSender");
const pinnedPopupPreview = document.getElementById("pinnedPopupPreview");
const pinnedUnpinBtn = document.getElementById("pinnedUnpinBtn");
const roomContextMenu = document.getElementById("roomContextMenu");
const markReadBtn = document.getElementById("markReadBtn");
const globalRoomToasts = document.getElementById("globalRoomToasts");
const filePreviewModal = document.getElementById("filePreviewModal");
const filePreviewBackdrop = document.getElementById("filePreviewBackdrop");
const filePreviewName = document.getElementById("filePreviewName");
const filePreviewBody = document.getElementById("filePreviewBody");
const filePreviewCloseBtn = document.getElementById("filePreviewCloseBtn");
const filePreviewDownloadBtn = document.getElementById("filePreviewDownloadBtn");

const SCREENS = { lobby: lobbyDiv, createRoom: createRoomDiv, passwordPrompt: passwordPromptDiv, nicknameEntry: nicknameEntryDiv, chat: chatDiv };
function showScreen(name) {
    Object.keys(SCREENS).forEach((key) => SCREENS[key].classList.toggle("hidden", key !== name));
}

// 아직 어느 방도 화면에 띄워보고 있지 않은(로비/방 만들기/비밀번호/닉네임
// 단계) 상태인지. 서버 쪽으로는 여러 방에 동시에 속해 있을 수 있지만, 화면에는
// 항상 최대 하나의 방만 "활성"으로 띄운다 - inRoom은 그 여부를 가리킨다.
let inRoom = false;
// 지금 보낸 로비 단계 요청이 무엇에 대한 것인지 - "ERR" 응답을 어느 화면의
// 에러 문구로 보여줄지 결정하는 데 쓴다.
let pendingAction = null; // "create" | "join" | "nickname" | null
let pendingRoomName = null;
let currentRoomName = null;

// 로비의 "내 채팅방" 탭에 보여줄, 지금 참여 중인 방들의 요약 정보(제목은
// Map의 key인 방 이름 자체). MYROOMS(일괄 동기화)/ROOMUPDATE(실시간 갱신)로
// 채워진다 - 지금 화면에 그 탭이 보이고 있지 않아도 계속 최신으로 유지된다.
const myRoomsData = new Map(); // roomName -> { count, unread, preview }
let activeLobbyTab = "myRooms";
let roomContextMenuTarget = null; // 우클릭한 "내 채팅방" 항목의 방 이름

// 상단 알람 팝업을 눌러서 다른 방으로 전환했을 때, 그 방의 히스토리 재생이
// 끝나는 시점(HISTORY_END)에 바로 이 메시지로 통통 튀며 이동하기 위해 잠시
// 기억해두는 대상. null이면 대기 중인 이동이 없다는 뜻.
let pendingAlertJump = null; // { roomName, msgId }

// 현재 방(화면에 띄워놓고 보는 중인 그 방)에 있는, 나를 제외한 사람들의
// 닉네임 집합. 귓속말 자동완성 팝업과 "/w 이름" 유효성 검사에 쓴다.
// JOIN/LEAVE/RENAME/ROSTER로 갱신된다.
const participants = new Set();

// 현재 방장 닉네임. null이면 아직 서버로부터 OWNER를 못 받은 상태(=방장을
// 모름). 처음 받는 OWNER는 그냥 초기 안내이고, 이미 알던 방장과 다른 이름의
// OWNER가 오면 그게 실제 "승급" 이벤트다.
let currentOwner = null;

// 지금 방에 상단 고정된 메시지(팝업으로 보여줌). null이면 고정된 게 없다는 뜻.
let pinnedMessage = null; // { msgId, sender, isFile, fileId, fileSize, text }

let muteTimerId = null;
let muteRemaining = 0;

// 서버 팔레트(multi_server.cc의 COLOR_PALETTE)와 이름이 정확히 일치해야 한다.
const COLOR_HEX = {
    red: "#e53935",
    orange: "#fb8c00",
    yellow: "#c9a227",
    green: "#43a047",
    teal: "#00897b",
    blue: "#1e88e5",
    purple: "#8e24aa",
    pink: "#d81b60",
    gray: "#616161",
};

// 닉네임 -> 커스텀 색상 이름("blue" 등). 없으면 기본색으로 표시.
// JOIN/COLORS(조용한 초기 동기화)/COLOR(실시간 변경 알림)로 갱신된다.
const userColors = new Map();

// 차단한 닉네임 집합. 순전히 이 브라우저 탭(지금 보고 있는 방 한정)만의 로컬
// 설정이라 서버에는 알리지 않는다 - 방을 나가거나 다른 방으로 옮기거나
// 새로고침하면 초기화된다.
const blockedUsers = new Set();

// 지금 답장을 작성 중인 대상. null이면 답장 모드가 아니다. 한 번 보내고 나면
// (또는 x를 누르면) 다시 null로 돌아가는 1회성 모드다.
let replyTarget = null; // { sender, text }

// 지금 수정 중인 내 메시지. null이면 수정 모드가 아니다. 답장과 마찬가지로
// 보내고 나면 자동으로 꺼지는 1회성 모드이고, 서로 동시에 켜질 수 없다
// (하나를 시작하면 다른 하나는 취소된다).
let editTarget = null; // { id }

// 메시지 우클릭 메뉴가 지금 어떤 메시지를 가리키고 있는지.
let ctxMenuTarget = null; // { id, text }

// --- 새 메시지가 와도 사용자가 위로 스크롤해서 과거를 보고 있으면 억지로
// 아래로 끌어내리지 않고, 대신 우측 하단에 "맨 아래로" 버튼을 띄운다. ---
const SCROLL_BOTTOM_THRESHOLD = 40; // 이 정도(px) 안이면 "이미 맨 아래"로 본다

function isNearBottom() {
    return logDiv.scrollHeight - logDiv.scrollTop - logDiv.clientHeight < SCROLL_BOTTOM_THRESHOLD;
}

function updateScrollToBottomBtn() {
    scrollToBottomBtn.classList.toggle("hidden", isNearBottom());
}

// append* 함수들이 새 내용을 로그에 붙이기 직전/직후에 감싸 쓴다: 붙이기 전
// 스크롤이 이미 맨 아래 근처였을 때만 붙인 뒤 다시 맨 아래로 따라가고, 아니면
// (과거 메시지를 읽는 중이면) 사용자가 보던 위치를 그대로 유지한다.
function scrollLogAfterAppend(wasNearBottom) {
    if (wasNearBottom) logDiv.scrollTop = logDiv.scrollHeight;
    updateScrollToBottomBtn();
}

logDiv.addEventListener("scroll", updateScrollToBottomBtn);
scrollToBottomBtn.addEventListener("click", () => {
    logDiv.scrollTop = logDiv.scrollHeight;
    updateScrollToBottomBtn();
});

function getUserColorHex(name) {
    const colorName = userColors.get(name);
    return colorName ? COLOR_HEX[colorName] : null;
}

const EMOJIS = [
    "😀","😂","😅","😊","😍","😘","😜","🤔",
    "😎","😭","😡","😱","👍","👎","👏","🙏",
    "🎉","❤️","🔥","✨","💯","👋","😴","🤗",
];

// 메시지 반응에 쓰는 고정된 소수 이모티콘 세트(multi_server.cc의
// REACTION_EMOJIS와 정확히 일치해야 한다 - 서버가 화이트리스트로 검증한다).
const REACTION_EMOJIS = ["👍", "❤️", "😂", "😮", "😢", "🙏"];

function appendSystem(text) {
    const wasNearBottom = isNearBottom();

    // 입장/퇴장/닉네임 변경 같은 시스템 알림이 중간에 끼면, 그 다음 메시지는
    // 같은 사람이 이어 보낸 것이라도 새 묶음으로 이름/시간을 다시 보여준다.
    currentGroupDiv = null;

    const div = document.createElement("div");
    div.className = "system";
    div.textContent = text;
    logDiv.appendChild(div);

    scrollLogAfterAppend(wasNearBottom);
}

// 이 탭에서 마지막으로 구분선을 띄운 날짜(로컬 기준). 날짜가 바뀔 때만 다시 띄운다.
let lastDividerKey = null;

function dateKey(d) {
    return d.getFullYear() + "-" + (d.getMonth() + 1) + "-" + d.getDate();
}

function formatDateDivider(d) {
    return `----${d.getFullYear()}년 ${d.getMonth() + 1}월 ${d.getDate()}일----`;
}

// "5:41 pm" 형식: 12시간제, 시는 0 채움 없음, 분은 2자리, am/pm은 소문자.
function formatTime(d) {
    let hours = d.getHours();
    const minutes = d.getMinutes().toString().padStart(2, "0");
    const ampm = hours < 12 ? "am" : "pm";
    hours = hours % 12;
    if (hours === 0) hours = 12;
    return `${hours}:${minutes} ${ampm}`;
}

function maybeInsertDateDivider(now) {
    const key = dateKey(now);
    if (key === lastDividerKey) return false;
    lastDividerKey = key;
    const div = document.createElement("div");
    div.className = "date-divider";
    div.textContent = formatDateDivider(now);
    logDiv.appendChild(div);
    return true;
}

// 같은 사람이 이 시간 이내에 연달아 보낸 메시지는 하나의 묶음으로 합쳐서
// 보낸사람/시간을 매번 반복하지 않는다.
const GROUP_WINDOW_MS = 3 * 60 * 1000;

let groupUser = null;
let groupLastTime = null;
let groupLastTimeSpan = null;
let currentGroupDiv = null;

// 이름 라벨(.group-name)을 표시할 텍스트/색을 만든다. 방장이면 왕관 이모티콘을
// 붙이고, 커스텀 색상이 있으면 그 색으로 칠한다.
function styleNameLabel(el, name) {
    el.textContent = name + (name === currentOwner ? " 👑" : "");
    const hex = getUserColorHex(name);
    if (hex) el.style.color = hex;
}

// --- @멘션 감지/알림 ---
// 서버는 메시지 본문을 그냥 문자열로만 다루므로, "@닉네임"을 찾아내는 건
// 순전히 클라이언트 몫이다 - 모든 클라이언트가 같은 MSG 브로드캐스트를
// 받으므로, 각자 자기 이름이 언급됐는지 로컬에서 판단하면 된다.
// 닉네임에 공백이 들어갈 수도 있지만(서버가 허용), "@" 뒤 공백 없는 덩어리를
// 토큰으로 본다 - 자동완성으로 고르면 항상 이 형태로 들어가므로 실사용에는
// 문제없다. "@"는 줄 시작이거나 공백 뒤일 때만 멘션으로 본다 - 안 그러면
// "email@example.com"처럼 붙어있는 "@"까지 멘션으로 잘못 잡아낸다(자동완성
// 트리거 AT_TRIGGER_RE와 같은 규칙).
function extractMentionedTokens(text) {
    const re = /(^|\s)@([^\s@]+)/g;
    const tokens = [];
    let m;
    while ((m = re.exec(text)) !== null) {
        tokens.push(m[2]);
    }
    return tokens;
}

// sender가 보낸 메시지가 나를 멘션했는지 보고, 그렇다면 토스트를 띄운다.
// "@all"은 그 순간 sender가 방장이었을 때만 전체 멘션으로 인정한다(방장이
// 아닌 사람이 UI를 거치지 않고 직접 "@all"을 쳐도 무시되도록).
function handleMentions(sender, text, msgDiv) {
    const tokens = extractMentionedTokens(text);
    const mentionsMe = tokens.includes(username);
    const mentionsAll = tokens.includes("all") && sender === currentOwner;
    if (!mentionsMe && !mentionsAll) return;
    showMentionToast(sender, text, msgDiv.dataset.msgId, mentionsAll && !mentionsMe);
}

function showMentionToast(sender, text, msgId, isAll) {
    const toast = document.createElement("div");
    toast.className = "mention-toast";

    const senderDiv = document.createElement("div");
    senderDiv.className = "toast-sender";
    senderDiv.textContent = sender + "님이 " + (isAll ? "전체 멘션했습니다" : "회원님을 멘션했습니다");
    toast.appendChild(senderDiv);

    const previewDiv = document.createElement("div");
    previewDiv.className = "toast-preview";
    previewDiv.textContent = text;
    toast.appendChild(previewDiv);

    toast.addEventListener("click", () => {
        jumpToMessage(msgId);
        toast.remove();
    });

    mentionToasts.appendChild(toast);
    setTimeout(() => { if (toast.parentNode) toast.remove(); }, 8000);
}

// 지금 보고 있지 않은 다른 방에 새 채팅 메시지가 왔을 때(ROOMUPDATE의
// isMsg=1) 뷰포트 상단 중앙에 크게 띄우는 알람 팝업. 멘션 토스트와 달리
// #chat 카드 안이 아니라 body 바로 아래(#globalRoomToasts)에 떠서, 로비를
// 보고 있든 다른 방을 보고 있든 항상 보인다. 누르면 그 방으로 전환하고,
// 히스토리 재생이 끝나는 대로 그 메시지로 통통 튀며 이동한다(멘션 토스트를
// 눌렀을 때와 동일한 연출).
function showRoomAlertToast(roomName, msgId, preview) {
    const toast = document.createElement("div");
    toast.className = "room-alert-toast";

    const roomDiv = document.createElement("div");
    roomDiv.className = "room-alert-toast-room";
    roomDiv.textContent = roomName;
    toast.appendChild(roomDiv);

    const previewDiv = document.createElement("div");
    previewDiv.className = "room-alert-toast-preview";
    previewDiv.textContent = preview;
    toast.appendChild(previewDiv);

    toast.addEventListener("click", () => {
        pendingAlertJump = { roomName, msgId };
        activateRoom(roomName);
        toast.remove();
    });

    globalRoomToasts.appendChild(toast);
    setTimeout(() => { if (toast.parentNode) toast.remove(); }, 8000);
}

// msgDiv 위치로 스크롤하고, 말풍선을 위로 3번 통통 튀겨서 어떤 메시지인지
// 직관적으로 보여준다. 멘션 팝업/답장 인용구 클릭 둘 다 이걸로 이동한다.
function bounceToMsgDiv(msgDiv) {
    if (!msgDiv) return;
    msgDiv.scrollIntoView({ behavior: "smooth", block: "center" });

    const bubble = msgDiv.querySelector(".bubble");
    if (!bubble) return;
    bubble.classList.remove("mention-bounce");
    void bubble.offsetWidth; // 같은 클래스를 다시 붙여도 애니메이션이 재생되도록 리플로우를 강제한다.
    bubble.classList.add("mention-bounce");
    bubble.addEventListener("animationend", () => bubble.classList.remove("mention-bounce"), { once: true });
}

// 멘션 토스트를 클릭했을 때: 이 클라이언트가 그 메시지를 렌더링할 때 매긴
// id로 바로 찾는다(지금 화면에 떠 있는 방 안에서만 왔다갔다하므로 다른
// 사람의 화면과 안 맞을 걱정이 없다).
function jumpToMessage(msgId) {
    bounceToMsgDiv(logDiv.querySelector('[data-msg-id="' + msgId + '"]'));
}

// 답장 인용구를 클릭했을 때: 답장은 "누가 보냈는지(origSender) + 내용
// (origPreview)"만 서버를 거쳐 전달되고, msgId는 클라이언트마다 다르게
// 매겨지므로(각자 자기 화면에서 독립적으로 렌더링하니까) 그대로 못 쓴다.
// 대신 이 화면에 실제로 렌더링된 메시지들 중 내용이 일치하는 걸 뒤에서부터
// (최근 것부터) 찾는다. 귓속말 묶음은 답장 대상이 될 수 없으므로 건너뛴다.
function jumpToOriginalReplyMessage(origSender, origPreview) {
    const truncated = origPreview.endsWith("…");
    const needle = truncated ? origPreview.slice(0, -1) : origPreview;

    const groups = [...logDiv.querySelectorAll(".msg-group")];
    for (let gi = groups.length - 1; gi >= 0; gi--) {
        const group = groups[gi];
        if (group.querySelector(".bubble.whisper")) continue;

        const nameEl = group.querySelector(".group-name");
        if (!nameEl) continue;
        if (nameEl.textContent.replace(/ 👑$/, "") !== origSender) continue;

        const bubbles = [...group.querySelectorAll(".msg > .bubble")];
        for (let bi = bubbles.length - 1; bi >= 0; bi--) {
            const text = bubbles[bi].textContent;
            const matches = truncated ? text.startsWith(needle) : text === needle;
            if (matches) {
                bounceToMsgDiv(bubbles[bi].closest(".msg"));
                return;
            }
        }
    }
}

// 말풍선에 "클릭하면 답장 시작", "우클릭하면 수정/고정/반응/삭제 메뉴"를
// 연결한다. 클로저로 캡처해둔 옛 text가 아니라 항상 지금 실제로 표시된
// 텍스트(bubbleSpan.textContent)를 쓴다 - 메시지가 수정된 뒤에 눌러도 최신
// 내용 기준으로 동작해야 하므로. 삭제되지 않았다면 누구의 메시지든 우클릭
// 메뉴 자체는 열린다(이모티콘 반응은 누구나 가능) - 메뉴 안에서 수정/삭제는
// 내 메시지일 때만, 고정은 내가 방장일 때만 openMessageContextMenu가 따로
// 숨긴다.
function wireMessageInteractions(msgDiv, bubbleSpan, sender) {
    // bubbleSpan이 아니라 msgDiv(행 전체)에 붙인다 - 정확히 말풍선 텍스트
    // 위가 아니라 살짝 옆(패딩 등)을 우클릭해도 반응하도록.
    msgDiv.addEventListener("click", () => {
        if (msgDiv.dataset.deleted === "1") return;
        startReply(sender, bubbleSpan.textContent);
    });
    msgDiv.addEventListener("contextmenu", (e) => {
        if (msgDiv.dataset.deleted === "1") return;
        const isMine = sender === username;
        const isHost = currentOwner === username;
        e.preventDefault();
        // stopPropagation이 없으면 이 이벤트가 document까지 버블링되고,
        // "메뉴 바깥을 우클릭하면 닫는다" 리스너가 "메뉴를 연 바로 이
        // 클릭"을 바깥 클릭으로 오인해서 열자마자 즉시 닫아버린다.
        e.stopPropagation();
        openMessageContextMenu(e.clientX, e.clientY, msgDiv.dataset.msgId, bubbleSpan.textContent, false, isMine, isHost);
    });
}

// 파일 말풍선의 상호작용: 클릭하면 답장이 아니라 미리보기 팝업이 뜨고,
// 우클릭 메뉴는 삭제되지 않았다면 누구든 열리되(이모티콘 반응은 누구나
// 가능) 수정하기는 애초에 없고(파일은 수정할 내용이라는 개념이 없으므로),
// 고정하기/삭제하기만 각각 방장/내 파일일 때만 openMessageContextMenu가
// 보여준다. 텍스트 메시지와 마찬가지로 방장은 남의 파일도 고정하러 우클릭할
// 수 있다.
function wireFileInteractions(msgDiv, sender, fileId, fileName) {
    msgDiv.addEventListener("click", () => {
        if (msgDiv.dataset.deleted === "1") return;
        openFilePreview(fileId, fileName);
    });
    msgDiv.addEventListener("contextmenu", (e) => {
        if (msgDiv.dataset.deleted === "1") return;
        const isMine = sender === username;
        const isHost = currentOwner === username;
        e.preventDefault();
        e.stopPropagation();
        openMessageContextMenu(e.clientX, e.clientY, msgDiv.dataset.msgId, null, true, isMine, isHost);
    });
}

// 새 메시지/파일을 now 시각에 붙일 때, 같은 사람이 이어보낸 걸로 묶을지
// 새 묶음을 열지 정하고, 필요하면 새 묶음 뼈대(.msg-group + 이름표)를
// currentGroupDiv에 채워넣는다. appendMessage/appendFileMessage가 공유한다.
function prepareMessageGroup(user, now, dividerInserted) {
    const continuesGroup = !dividerInserted &&
        groupUser === user &&
        groupLastTime !== null &&
        (now - groupLastTime) < GROUP_WINDOW_MS &&
        currentGroupDiv !== null;

    if (!continuesGroup) {
        currentGroupDiv = document.createElement("div");
        currentGroupDiv.className = "msg-group";
        logDiv.appendChild(currentGroupDiv);

        const nameDiv = document.createElement("div");
        nameDiv.className = "group-name";
        styleNameLabel(nameDiv, user);
        currentGroupDiv.appendChild(nameDiv);
    } else if (groupLastTimeSpan) {
        // 이 묶음의 마지막 메시지 옆에만 시간이 붙어야 하므로, 직전 메시지의
        // 시간 표시는 지금 지운다(잠시 후 이 메시지에 새로 붙임).
        groupLastTimeSpan.remove();
        groupLastTimeSpan = null;
    }
}

// prepareMessageGroup 이후 msgDiv를 실제로 붙이고 나서 공통으로 해야 할 뒷정리
// (스크롤 추적 + 다음 메시지가 이어붙을지 판단할 그룹 상태 갱신).
function finishMessageAppend(user, now, timeSpan, wasNearBottom) {
    scrollLogAfterAppend(wasNearBottom);
    groupUser = user;
    groupLastTime = now;
    groupLastTimeSpan = timeSpan;
}

// 메시지 하나(id)의 반응 뱃지 줄을 msgDiv 바로 뒤에 끼워넣는다. 반응이 아직
// 없으므로 처음엔 항상 비어서 hidden - REACTIONS가 도착하면 applyReactions가
// data-reactions-for로 이 줄을 찾아 채운다. parentDiv는 msgDiv를 담고 있는
// 그룹 div(currentGroupDiv 또는 답장의 groupDiv)로, msgDiv 바로 다음 형제로
// 붙여야 그 메시지 바로 아래에 뜬다.
function attachReactionsRow(parentDiv, id) {
    const row = document.createElement("div");
    row.className = "msg-reactions hidden";
    row.dataset.reactionsFor = String(id);
    parentDiv.appendChild(row);
    return row;
}

// 한 묶음(.msg-group)은 이름을 맨 위에 한 번만 보여주고, 그 아래로 같은
// 사람이 연달아 보낸 말풍선들을 세로로 쌓는다. id는 서버가 매긴 메시지
// 고유번호로, 수정/삭제/멘션 이동/답장 인용구를 이걸로 찾는다.
function appendMessage(user, text, id) {
    const wasNearBottom = isNearBottom();
    const now = new Date();
    const dividerInserted = maybeInsertDateDivider(now);
    prepareMessageGroup(user, now, dividerInserted);

    const msgDiv = document.createElement("div");
    msgDiv.className = "msg";
    msgDiv.dataset.msgId = String(id);

    const bubbleSpan = document.createElement("span");
    bubbleSpan.className = "bubble";
    bubbleSpan.textContent = text;
    wireMessageInteractions(msgDiv, bubbleSpan, user);
    msgDiv.appendChild(bubbleSpan);

    const timeSpan = document.createElement("span");
    timeSpan.className = "time";
    timeSpan.textContent = formatTime(now);
    msgDiv.appendChild(timeSpan);

    currentGroupDiv.appendChild(msgDiv);
    attachReactionsRow(currentGroupDiv, id);
    finishMessageAppend(user, now, timeSpan, wasNearBottom);

    return msgDiv;
}

// 파일 크기를 "12.3 KB"/"4.5 MB" 같은 사람이 읽기 쉬운 형태로 바꾼다.
function formatFileSize(bytes) {
    if (bytes < 1024) return bytes + " B";
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
    return (bytes / (1024 * 1024)).toFixed(1) + " MB";
}

// 첨부 파일 하나를 말풍선으로 그린다. appendMessage와 그룹핑 규칙을 그대로
// 공유하되(같은 사람이 텍스트 메시지 사이에 파일을 보내도 한 묶음으로
// 이어진다), 클릭하면 답장이 아니라 미리보기 팝업이 뜬다.
function appendFileMessage(user, fileId, fileName, fileSize, id) {
    const wasNearBottom = isNearBottom();
    const now = new Date();
    const dividerInserted = maybeInsertDateDivider(now);
    prepareMessageGroup(user, now, dividerInserted);

    const msgDiv = document.createElement("div");
    msgDiv.className = "msg";
    msgDiv.dataset.msgId = String(id);
    msgDiv.dataset.fileId = fileId;

    const bubbleSpan = document.createElement("span");
    bubbleSpan.className = "bubble file";

    const iconSpan = document.createElement("span");
    iconSpan.className = "file-icon";
    iconSpan.textContent = "📎";
    bubbleSpan.appendChild(iconSpan);

    const infoSpan = document.createElement("span");
    infoSpan.className = "file-info";
    const nameSpan = document.createElement("span");
    nameSpan.className = "file-name";
    nameSpan.textContent = fileName;
    const sizeSpan = document.createElement("span");
    sizeSpan.className = "file-size";
    sizeSpan.textContent = formatFileSize(fileSize);
    infoSpan.appendChild(nameSpan);
    infoSpan.appendChild(sizeSpan);
    bubbleSpan.appendChild(infoSpan);

    wireFileInteractions(msgDiv, user, fileId, fileName);
    msgDiv.appendChild(bubbleSpan);

    const timeSpan = document.createElement("span");
    timeSpan.className = "time";
    timeSpan.textContent = formatTime(now);
    msgDiv.appendChild(timeSpan);

    currentGroupDiv.appendChild(msgDiv);
    attachReactionsRow(currentGroupDiv, id);
    finishMessageAppend(user, now, timeSpan, wasNearBottom);

    return msgDiv;
}

// 가장 최근에 귓속말을 주고받은 상대. "/r" 답장 단축 명령에 쓴다.
let lastWhisperPartner = null;

// 귓속말은 일반 메시지 묶음과는 섞이지 않는다. 내가 보낸 건 "상대님에게",
// 내가 받은 건 그냥 "보낸사람" 라벨을 이름 자리에 보여준다.
function appendWhisper(from, to, text, sentByMe) {
    const wasNearBottom = isNearBottom();
    const now = new Date();
    maybeInsertDateDivider(now);

    lastWhisperPartner = sentByMe ? to : from;

    // 이 귓속말 다음에 오는 일반 메시지가 예전 묶음에 이어붙지 않도록 끊는다.
    currentGroupDiv = null;

    const groupDiv = document.createElement("div");
    groupDiv.className = "msg-group";
    logDiv.appendChild(groupDiv);

    const nameDiv = document.createElement("div");
    nameDiv.className = "group-name";
    // styleNameLabel은 색상/왕관을 실제 닉네임 기준으로 판단하므로, 라벨
    // 텍스트("OO님에게" 같은)는 색만 입힌 뒤 직접 덮어쓴다.
    const whisperOwnerName = sentByMe ? to : from;
    styleNameLabel(nameDiv, whisperOwnerName);
    nameDiv.textContent = (sentByMe ? (to + "님에게") : from) + (whisperOwnerName === currentOwner ? " 👑" : "");
    groupDiv.appendChild(nameDiv);

    const msgDiv = document.createElement("div");
    msgDiv.className = "msg";

    const bubbleSpan = document.createElement("span");
    bubbleSpan.className = "bubble whisper";
    bubbleSpan.textContent = text;
    if (!sentByMe) {
        // 받은 귓속말은 클릭하면 그 사람에게 바로 답장할 수 있게 "/w 이름 "을 채워준다.
        bubbleSpan.classList.add("clickable");
        bubbleSpan.addEventListener("click", () => {
            msgInput.value = "/w " + from + " ";
            msgInput.focus();
            const caret = msgInput.value.length;
            msgInput.setSelectionRange(caret, caret);
        });
    }
    msgDiv.appendChild(bubbleSpan);

    const timeSpan = document.createElement("span");
    timeSpan.className = "time";
    timeSpan.textContent = formatTime(now);
    msgDiv.appendChild(timeSpan);

    groupDiv.appendChild(msgDiv);
    scrollLogAfterAppend(wasNearBottom);
}

// 답장은 귓속말과 마찬가지로 일반 메시지 묶음과 섞이지 않고 독립된 묶음으로
// 뜬다. 이름 아래에 원본 메시지 인용구를 붙이고, 그 아래에 실제 답장 내용을
// 붙인다. 인용구를 누르면 원본 메시지 위치로 이동하고(3번 통통 튐), 답장
// 내용 말풍선을 누르면 이 답장 자체에 또 답장을 시작할 수 있다(일반 메시지와
// 동일한 동작). id는 서버가 매긴 이 답장 메시지 자신의 고유번호다.
function appendReply(sender, origSender, origPreview, body, id) {
    const wasNearBottom = isNearBottom();
    const now = new Date();
    maybeInsertDateDivider(now);

    currentGroupDiv = null;

    const groupDiv = document.createElement("div");
    groupDiv.className = "msg-group";
    logDiv.appendChild(groupDiv);

    const nameDiv = document.createElement("div");
    nameDiv.className = "group-name";
    styleNameLabel(nameDiv, sender);
    groupDiv.appendChild(nameDiv);

    const quoteDiv = document.createElement("div");
    quoteDiv.className = "reply-quote";
    quoteDiv.textContent = "↩ " + origSender + "님에게 답장: " + origPreview;
    quoteDiv.addEventListener("click", () => jumpToOriginalReplyMessage(origSender, origPreview));
    groupDiv.appendChild(quoteDiv);

    const msgDiv = document.createElement("div");
    msgDiv.className = "msg";
    msgDiv.dataset.msgId = String(id);

    const bubbleSpan = document.createElement("span");
    bubbleSpan.className = "bubble";
    bubbleSpan.textContent = body;
    wireMessageInteractions(msgDiv, bubbleSpan, sender);
    msgDiv.appendChild(bubbleSpan);

    const timeSpan = document.createElement("span");
    timeSpan.className = "time";
    timeSpan.textContent = formatTime(now);
    msgDiv.appendChild(timeSpan);

    groupDiv.appendChild(msgDiv);
    attachReactionsRow(groupDiv, id);
    scrollLogAfterAppend(wasNearBottom);

    return msgDiv;
}

// 미리보기는 프로토콜 필드 경계를 깨지 않도록 "|"/개행을 제거하고, 너무
// 길면 잘라서 서버로 보낸다(서버도 방어적으로 한 번 더 정리하긴 하지만).
function buildReplyPreview(text) {
    const cleaned = text.replace(/[|\r\n]/g, " ");
    return cleaned.length > 60 ? cleaned.slice(0, 60) + "…" : cleaned;
}

function startReply(sender, text) {
    editTarget = null; // 답장을 시작하면 수정 모드는 취소한다(둘은 동시에 켜질 수 없음)
    replyTarget = { sender, text };
    replyPreviewText.innerHTML = "";
    const b = document.createElement("b");
    b.textContent = sender;
    replyPreviewText.appendChild(b);
    replyPreviewText.appendChild(document.createTextNode("님에게 답장: " + text));
    replyPreview.classList.remove("hidden");
    msgInput.focus();
}

function cancelReply() {
    replyTarget = null;
    replyPreview.classList.add("hidden");
}

replyPreviewCancel.addEventListener("click", cancelReply);

// --- 메시지 우클릭 메뉴(수정/고정/삭제) ---

function openMessageContextMenu(x, y, id, text, isFile, isMine, isHost) {
    ctxMenuTarget = { id, text, isFile: !!isFile };
    // 수정/삭제는 내 메시지일 때만(그리고 수정은 파일이 아닐 때만), 고정은
    // 방장일 때만 보여준다 - 방장이 남의 메시지를 우클릭했을 때는 고정하기만 뜬다.
    // 이모티콘으로 반응하기는 누구의 메시지든 항상 보여준다(소유권을 따지지 않음).
    ctxEditBtn.classList.toggle("hidden", !isMine || !!isFile);
    ctxDeleteBtn.classList.toggle("hidden", !isMine);
    ctxPinBtn.classList.toggle("hidden", !isHost);
    messageContextMenu.style.left = x + "px";
    messageContextMenu.style.top = y + "px";
    messageContextMenu.classList.remove("hidden");
}

function closeMessageContextMenu() {
    ctxMenuTarget = null;
    messageContextMenu.classList.add("hidden");
}

document.addEventListener("click", (e) => {
    if (messageContextMenu.classList.contains("hidden")) return;
    if (messageContextMenu.contains(e.target)) return;
    closeMessageContextMenu();
});
document.addEventListener("contextmenu", (e) => {
    if (messageContextMenu.classList.contains("hidden")) return;
    if (messageContextMenu.contains(e.target)) return;
    closeMessageContextMenu();
});

ctxEditBtn.addEventListener("click", () => {
    if (!ctxMenuTarget) return;
    cancelReply(); // 수정을 시작하면 답장 모드는 취소한다
    editTarget = { id: ctxMenuTarget.id };
    msgInput.value = ctxMenuTarget.text;
    closeMessageContextMenu();
    msgInput.focus();
    const caret = msgInput.value.length;
    msgInput.setSelectionRange(caret, caret);
});

ctxPinBtn.addEventListener("click", () => {
    if (!ctxMenuTarget) return;
    const id = ctxMenuTarget.id;
    closeMessageContextMenu();
    ws.send("PIN|" + currentRoomName + "|" + id);
});

ctxDeleteBtn.addEventListener("click", () => {
    if (!ctxMenuTarget) return;
    const id = ctxMenuTarget.id;
    closeMessageContextMenu();
    if (!confirm("정말로 삭제하시겠습니까?")) return;
    ws.send("DELETE|" + currentRoomName + "|" + id);
});

// --- 이모티콘 반응 선택 팝업 ---
// 우클릭 메뉴에서 "이모티콘으로 반응하기"를 누르면, 메뉴는 닫고 같은
// 자리에 이모티콘 6개짜리 작은 팝업을 연다(우클릭 메뉴처럼 좌표 기반
// position: fixed). 여기서 고른 이모티콘을 그대로 REACT로 보낸다 - 서버가
// 토글/교체 로직을 갖고 있으므로 클라이언트는 그냥 요청만 하면 된다.
let reactionPickerTarget = null;

REACTION_EMOJIS.forEach((emoji) => {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.textContent = emoji;
    btn.addEventListener("click", () => {
        if (!reactionPickerTarget) return;
        ws.send("REACT|" + currentRoomName + "|" + reactionPickerTarget + "|" + emoji);
        closeReactionPicker();
    });
    reactionPicker.appendChild(btn);
});

function openReactionPicker(x, y, id) {
    reactionPickerTarget = id;
    reactionPicker.style.left = x + "px";
    reactionPicker.style.top = y + "px";
    reactionPicker.classList.remove("hidden");
}

function closeReactionPicker() {
    reactionPickerTarget = null;
    reactionPicker.classList.add("hidden");
}

document.addEventListener("click", (e) => {
    if (reactionPicker.classList.contains("hidden")) return;
    if (reactionPicker.contains(e.target)) return;
    closeReactionPicker();
});
document.addEventListener("contextmenu", (e) => {
    if (reactionPicker.classList.contains("hidden")) return;
    if (reactionPicker.contains(e.target)) return;
    closeReactionPicker();
});

ctxReactBtn.addEventListener("click", (e) => {
    if (!ctxMenuTarget) return;
    // stopPropagation이 없으면 이 클릭이 document까지 버블링되고, "팝업
    // 바깥을 클릭하면 닫는다" 리스너가 방금 이 클릭을 바깥 클릭으로 오인해서
    // 열자마자 즉시 닫아버린다(우클릭 메뉴 자체를 열 때와 같은 문제).
    e.stopPropagation();
    const id = ctxMenuTarget.id;
    const x = messageContextMenu.style.left;
    const y = messageContextMenu.style.top;
    closeMessageContextMenu();
    openReactionPicker(parseInt(x, 10), parseInt(y, 10), id);
});

// --- 파일 미리보기 팝업 ---

function fileUrl(fileId) { return "/files/" + fileId; }
function isPreviewableImage(name) { return /\.(png|jpe?g|gif|webp|svg)$/i.test(name); }
function isPreviewablePdf(name) { return /\.pdf$/i.test(name); }

function openFilePreview(fileId, fileName) {
    filePreviewName.textContent = fileName;
    filePreviewBody.innerHTML = "";
    const url = fileUrl(fileId);

    if (isPreviewableImage(fileName)) {
        const img = document.createElement("img");
        img.src = url;
        img.alt = fileName;
        filePreviewBody.appendChild(img);
    } else if (isPreviewablePdf(fileName)) {
        const iframe = document.createElement("iframe");
        iframe.src = url;
        filePreviewBody.appendChild(iframe);
    } else {
        const notice = document.createElement("div");
        notice.className = "file-preview-unsupported";
        notice.textContent = "이 파일 형식은 미리보기를 지원하지 않습니다. 다운로드해서 확인해주세요.";
        filePreviewBody.appendChild(notice);
    }

    filePreviewDownloadBtn.href = url;
    filePreviewDownloadBtn.setAttribute("download", fileName);
    filePreviewModal.classList.remove("hidden");
}

function closeFilePreview() {
    filePreviewModal.classList.add("hidden");
    filePreviewBody.innerHTML = "";
}

filePreviewCloseBtn.addEventListener("click", closeFilePreview);
filePreviewBackdrop.addEventListener("click", closeFilePreview);

// --- 상단 고정 메시지 팝업 ---
// 말풍선이 아니라 헤더 바로 아래에 뜨는 별도 팝업 바다. 누르면(✕ 버튼
// 제외) 멘션 토스트/답장 인용구처럼 원본 메시지로 이동해서 통통 튄다.
// ✕(고정 해제)는 방장에게만 보인다.
function renderPinnedPopup() {
    if (!pinnedMessage) {
        pinnedPopup.classList.add("hidden");
        return;
    }
    pinnedPopupSender.textContent = pinnedMessage.sender;
    pinnedPopupPreview.textContent = pinnedMessage.isFile ? ("📎 " + pinnedMessage.text) : pinnedMessage.text;
    pinnedUnpinBtn.classList.toggle("hidden", currentOwner !== username);
    pinnedPopup.classList.remove("hidden");
}

pinnedPopup.addEventListener("click", (e) => {
    if (e.target === pinnedUnpinBtn || !pinnedMessage) return;
    jumpToMessage(pinnedMessage.msgId);
});

pinnedUnpinBtn.addEventListener("click", (e) => {
    e.stopPropagation(); // pinnedPopup 자체의 클릭(이동) 핸들러로 번지지 않도록
    ws.send("UNPIN|" + currentRoomName);
});

// 서버로부터 EDITED를 받았을 때: 말풍선 내용을 새 텍스트로 바꾸고, 시간
// 정보 앞에 "(수정됨)" 표시를 붙인다(이미 붙어있으면 중복으로 안 붙인다).
// EDITED/DELETED는 지금 보고 있는 방이 아니면(백그라운드 방이면) 애초에
// 이 화면에 그 메시지가 없으므로, 아래 querySelector가 못 찾아서 조용히
// 아무 일도 안 하고 넘어간다.
function applyEditedMessage(msgId, newText) {
    const msgDiv = logDiv.querySelector('[data-msg-id="' + msgId + '"]');
    if (!msgDiv) return;

    const bubble = msgDiv.querySelector(".bubble");
    if (bubble) bubble.textContent = newText;

    if (!msgDiv.querySelector(".edited-tag")) {
        const editedTag = document.createElement("span");
        editedTag.className = "edited-tag";
        editedTag.textContent = "(수정됨)";
        const timeSpan = msgDiv.querySelector(".time");
        if (timeSpan) msgDiv.insertBefore(editedTag, timeSpan);
        else msgDiv.appendChild(editedTag);
    }
}

// 서버로부터 DELETED를 받았을 때: 말풍선 내용을 안내 문구로 바꾸고, 더 이상
// 답장/수정/삭제 대상이 되지 않도록 표시해둔다(클릭 핸들러들이 이 표시를 본다).
function applyDeletedMessage(msgId) {
    const msgDiv = logDiv.querySelector('[data-msg-id="' + msgId + '"]');
    if (!msgDiv) return;

    const bubble = msgDiv.querySelector(".bubble");
    if (bubble) {
        bubble.textContent = "메시지가 삭제되었습니다";
        bubble.classList.add("deleted");
        bubble.classList.remove("file"); // 파일 말풍선이었다면 아이콘/파일명 레이아웃을 걷어낸다
    }
    const editedTag = msgDiv.querySelector(".edited-tag");
    if (editedTag) editedTag.remove();
    msgDiv.dataset.deleted = "1";

    // 삭제된 메시지에는 더 이상 반응을 붙일 수 없고(서버가 거부한다), 이미
    // 붙어있던 반응도 더 보여줄 내용이 없으므로 뱃지 줄 자체를 지운다.
    const reactionsRow = logDiv.querySelector('[data-reactions-for="' + msgId + '"]');
    if (reactionsRow) reactionsRow.remove();
}

// 서버로부터 REACTIONS를 받았을 때: msgId의 반응 뱃지 줄을 이모티콘별로
// 묶어서 다시 그린다. reactions는 [{ emoji, user }, ...] 형태(같은 이모티콘이
// 여러 개면 그만큼 반복해서 들어있다). 뱃지를 누르면 그 이모티콘으로 내
// 반응을 토글한다(이미 그 이모티콘으로 반응 중이면 취소, 아니면 그걸로
// 교체) - 서버가 사용자당 이모티콘 하나만 허용하는 토글 로직을 그대로
// 갖고 있으므로 클라이언트는 그냥 같은 REACT 명령을 다시 보내기만 하면 된다.
function applyReactions(msgId, reactions) {
    const row = logDiv.querySelector('[data-reactions-for="' + msgId + '"]');
    if (!row) return;

    row.innerHTML = "";
    if (reactions.length === 0) {
        row.classList.add("hidden");
        return;
    }

    const grouped = new Map(); // emoji -> [user, ...]
    reactions.forEach(({ emoji, user }) => {
        if (!grouped.has(emoji)) grouped.set(emoji, []);
        grouped.get(emoji).push(user);
    });

    grouped.forEach((users, emoji) => {
        const pill = document.createElement("button");
        pill.type = "button";
        pill.className = "reaction-pill" + (users.includes(username) ? " mine" : "");
        pill.textContent = emoji + " " + users.length;
        pill.addEventListener("click", () => {
            ws.send("REACT|" + currentRoomName + "|" + msgId + "|" + emoji);
        });
        row.appendChild(pill);
    });

    row.classList.remove("hidden");
}

// 방을 새로 열어볼 때(처음 입장 또는 백그라운드에서 재활성화) 서버가
// HISTMSG로 재현해주는 과거 메시지 하나를 그린다. appendMessage/appendReply/
// appendFileMessage를 그대로 재사용하되, 지금 막 도착한 새 이벤트가 아니라
// "이미 있었던 일"이므로 멘션 토스트는 띄우지 않는다. text는 이미 최신
// 내용(수정됐다면 수정된 내용, is_file이면 파일명)이므로 삭제/수정 표시만
// 덧붙이면 된다(파일은 애초에 수정될 수 없으므로 editedFlag가 안 선다).
function renderHistoryMessage(sender, id, text, deletedFlag, editedFlag, isReplyFlag, origSender, origPreview,
                               isFileFlag, fileId, fileSize) {
    const msgDiv = isFileFlag
        ? appendFileMessage(sender, fileId, text, fileSize, id)
        : isReplyFlag
            ? appendReply(sender, origSender, origPreview, text, id)
            : appendMessage(sender, text, id);

    if (deletedFlag) {
        applyDeletedMessage(id);
    } else if (editedFlag) {
        const editedTag = document.createElement("span");
        editedTag.className = "edited-tag";
        editedTag.textContent = "(수정됨)";
        const timeSpan = msgDiv.querySelector(".time");
        if (timeSpan) msgDiv.insertBefore(editedTag, timeSpan);
        else msgDiv.appendChild(editedTag);
    }
}

// 서버 프로토콜(TYPE|필드|...)을 최대 maxParts개로 분리한다.
// 마지막 조각은 남은 문자열 전체(구분자 포함 가능)를 담는다.
function splitProtocol(line, maxParts) {
    const parts = [];
    let rest = line;
    for (let i = 0; i < maxParts - 1; i++) {
        const idx = rest.indexOf("|");
        if (idx === -1) { parts.push(rest); rest = ""; break; }
        parts.push(rest.slice(0, idx));
        rest = rest.slice(idx + 1);
    }
    parts.push(rest);
    return parts;
}

// --- 방 목록("채팅방 찾기" 탭) ---

function parseRoomList(data) {
    const tokens = data.split("|").slice(1);
    const rooms = [];
    for (let i = 0; i + 3 <= tokens.length; i += 3) {
        rooms.push({
            name: tokens[i],
            count: parseInt(tokens[i + 1], 10),
            hasPassword: tokens[i + 2] === "1",
        });
    }
    return rooms;
}

function renderRoomList(roomsList) {
    const sorted = [...roomsList].sort((a, b) => a.name.localeCompare(b.name, "ko"));
    roomListDiv.innerHTML = "";
    if (sorted.length === 0) {
        const empty = document.createElement("div");
        empty.className = "room-list-empty";
        empty.textContent = "채팅방이 없습니다.";
        roomListDiv.appendChild(empty);
        return;
    }
    sorted.forEach((r) => {
        const item = document.createElement("div");
        item.className = "room-item";
        const nameSpan = document.createElement("span");
        nameSpan.className = "room-item-name";
        nameSpan.textContent = (r.hasPassword ? "🔒 " : "") + r.name;
        const countSpan = document.createElement("span");
        countSpan.className = "room-item-count";
        countSpan.textContent = r.count + "명";
        item.appendChild(nameSpan);
        item.appendChild(countSpan);
        item.addEventListener("click", () => attemptJoinRoom(r.name, r.hasPassword));
        roomListDiv.appendChild(item);
    });
}

function attemptJoinRoom(roomName, hasPassword) {
    pendingRoomName = roomName;
    if (hasPassword) {
        passwordPromptRoomName.textContent = roomName;
        passwordError.textContent = "";
        roomPasswordInput.value = "";
        showScreen("passwordPrompt");
        roomPasswordInput.focus();
    } else {
        pendingAction = "join";
        ws.send("JOIN_ROOM|" + roomName + "|");
    }
}

roomSearchBtn.addEventListener("click", () => {
    const q = roomSearchInput.value.trim();
    if (!q) { ws.send("ROOMS"); return; }
    ws.send("SEARCH|" + q);
});
roomSearchClearBtn.addEventListener("click", () => {
    roomSearchInput.value = "";
    ws.send("ROOMS");
});
roomSearchInput.addEventListener("keydown", (e) => {
    if (e.key === "Enter") roomSearchBtn.click();
});

showCreateRoomBtn.addEventListener("click", () => {
    newRoomName.value = "";
    document.querySelector('input[name="visibility"][value="public"]').checked = true;
    newRoomHasPassword.checked = false;
    newRoomPassword.value = "";
    newRoomPassword.classList.add("hidden");
    createRoomError.textContent = "";
    showScreen("createRoom");
});

newRoomHasPassword.addEventListener("change", () => {
    newRoomPassword.classList.toggle("hidden", !newRoomHasPassword.checked);
    if (!newRoomHasPassword.checked) newRoomPassword.value = "";
});

createRoomBackBtn.addEventListener("click", () => backToLobbyScreen());

createRoomSubmitBtn.addEventListener("click", () => {
    const name = newRoomName.value.trim();
    if (!name) {
        createRoomError.textContent = "채팅방 이름을 입력해주세요.";
        return;
    }
    const isPublic = document.querySelector('input[name="visibility"]:checked').value === "public";
    const password = newRoomHasPassword.checked ? newRoomPassword.value : "";
    pendingAction = "create";
    ws.send("CREATE|" + name + "|" + (isPublic ? "1" : "0") + "|" + password);
});

passwordBackBtn.addEventListener("click", () => backToLobbyScreen());

passwordSubmitBtn.addEventListener("click", () => {
    pendingAction = "join";
    ws.send("JOIN_ROOM|" + pendingRoomName + "|" + roomPasswordInput.value);
});
roomPasswordInput.addEventListener("keydown", (e) => {
    if (e.key === "Enter") passwordSubmitBtn.click();
});

nicknameBackBtn.addEventListener("click", () => {
    ws.send("CANCEL");
});

nicknameSubmitBtn.addEventListener("click", () => {
    const name = roomUsernameInput.value.trim();
    if (!name) return;
    pendingAction = "nickname";
    ws.send("JOIN|" + name);
});
roomUsernameInput.addEventListener("keydown", (e) => {
    if (e.key === "Enter") nicknameSubmitBtn.click();
});

// --- 로비 탭("내 채팅방" / "채팅방 찾기") ---

function showLobbyTab(tab) {
    activeLobbyTab = tab;
    myRoomsTabBtn.classList.toggle("active", tab === "myRooms");
    findRoomsTabBtn.classList.toggle("active", tab === "findRooms");
    myRoomsPanel.classList.toggle("hidden", tab !== "myRooms");
    findRoomsPanel.classList.toggle("hidden", tab !== "findRooms");
}

myRoomsTabBtn.addEventListener("click", () => {
    showLobbyTab("myRooms");
    ws.send("MYROOMS");
});
findRoomsTabBtn.addEventListener("click", () => {
    showLobbyTab("findRooms");
    ws.send("ROOMS");
});

// "채팅방 만들기/비밀번호" 같은 로비의 하위 화면에서 뒤로가기를 누르면 항상
// 로비로, 그것도 마지막으로 보던 탭 그대로 돌아간다.
function backToLobbyScreen() {
    showScreen("lobby");
    showLobbyTab(activeLobbyTab);
}

function parseMyRoomsMessage(data) {
    const tokens = data.split("|").slice(1);
    const rooms = [];
    for (let i = 0; i + 4 <= tokens.length; i += 4) {
        rooms.push({
            name: tokens[i],
            count: parseInt(tokens[i + 1], 10),
            unread: parseInt(tokens[i + 2], 10),
            preview: tokens[i + 3],
        });
    }
    return rooms;
}

function renderMyRoomList() {
    const rooms = [...myRoomsData.entries()]
        .map(([name, d]) => ({ name, ...d }))
        .sort((a, b) => a.name.localeCompare(b.name, "ko"));

    myRoomList.innerHTML = "";
    if (rooms.length === 0) {
        const empty = document.createElement("div");
        empty.className = "my-room-list-empty";
        empty.textContent = "참여 중인 채팅방이 없습니다.";
        myRoomList.appendChild(empty);
        return;
    }

    rooms.forEach((r) => {
        const item = document.createElement("div");
        item.className = "my-room-item";

        const main = document.createElement("div");
        main.className = "my-room-item-main";
        const titleRow = document.createElement("div");
        titleRow.className = "my-room-item-title-row";
        const title = document.createElement("div");
        title.className = "my-room-item-title";
        title.textContent = r.name;
        titleRow.appendChild(title);
        const countSpan = document.createElement("span");
        countSpan.className = "my-room-item-count";
        countSpan.textContent = r.count + "명";
        titleRow.appendChild(countSpan);
        main.appendChild(titleRow);
        const preview = document.createElement("div");
        preview.className = "my-room-item-preview";
        preview.textContent = r.preview || "";
        main.appendChild(preview);
        item.appendChild(main);

        if (r.unread > 0) {
            const badge = document.createElement("span");
            badge.className = "my-room-item-badge";
            badge.textContent = r.unread > 99 ? "99+" : String(r.unread);
            item.appendChild(badge);
        }

        item.addEventListener("click", () => activateRoom(r.name));
        item.addEventListener("contextmenu", (e) => {
            e.preventDefault();
            e.stopPropagation(); // messageContextMenu 쪽과 같은 이유로 필요
            openRoomContextMenu(e.clientX, e.clientY, r.name);
        });

        myRoomList.appendChild(item);
    });
}

function activateRoom(roomName) {
    ws.send("SWITCH_ROOM|" + roomName);
}

backToLobbyBtn.addEventListener("click", () => {
    // "뒤로가기": 방은 나가지 않고(서버 쪽 멤버십은 그대로) 화면만 로비로
    // 돌아간다. 서버에는 SWITCH_ROOM으로 "지금 아무 방도 안 보고 있다"만 알린다.
    ws.send("SWITCH_ROOM|");
    resetRoomState();
    backToLobbyScreen();
    ws.send("MYROOMS");
});

function leaveCurrentRoom() {
    if (!confirm("정말 이 방을 나가시겠습니까?")) return;
    ws.send("LEAVE_ROOM|" + currentRoomName);
    myRoomsData.delete(currentRoomName);
    resetRoomState();
    backToLobbyScreen();
    ws.send("MYROOMS");
}

leaveRoomBtn.addEventListener("click", leaveCurrentRoom);

// --- "내 채팅방" 항목 우클릭 메뉴(안읽음 모두 읽음으로 표시) ---

function openRoomContextMenu(x, y, roomName) {
    roomContextMenuTarget = roomName;
    roomContextMenu.style.left = x + "px";
    roomContextMenu.style.top = y + "px";
    roomContextMenu.classList.remove("hidden");
}

function closeRoomContextMenu() {
    roomContextMenuTarget = null;
    roomContextMenu.classList.add("hidden");
}

document.addEventListener("click", (e) => {
    if (roomContextMenu.classList.contains("hidden")) return;
    if (roomContextMenu.contains(e.target)) return;
    closeRoomContextMenu();
});
document.addEventListener("contextmenu", (e) => {
    if (roomContextMenu.classList.contains("hidden")) return;
    if (roomContextMenu.contains(e.target)) return;
    closeRoomContextMenu();
});

markReadBtn.addEventListener("click", () => {
    if (!roomContextMenuTarget) return;
    ws.send("MARK_READ|" + roomContextMenuTarget);
    // 서버 응답(ROOMUPDATE)을 기다리지 않고 낙관적으로 바로 배지를 지운다 -
    // "빨간색 숫자 안읽음 표시가 바로 없어지도록" 요구사항과 맞다.
    const existing = myRoomsData.get(roomContextMenuTarget);
    if (existing) { existing.unread = 0; renderMyRoomList(); }
    closeRoomContextMenu();
});

function connect() {
    ws = new WebSocket("ws://" + location.host + "/ws");

    ws.onopen = () => {
        ws.send("HELLO");
    };

    ws.onmessage = (ev) => {
        // MYROOMS/ROOMUPDATE는 로비 화면이든 채팅 화면이든 어느 상태에서나
        // 올 수 있다("지금 안 보고 있는 다른 방"에서 벌어진 일이므로) - 그래서
        // inRoom 여부로 나뉘는 아래 분기보다 먼저 처리한다.
        if (ev.data === "MYROOMS" || ev.data.startsWith("MYROOMS|")) {
            myRoomsData.clear();
            parseMyRoomsMessage(ev.data).forEach((r) => {
                myRoomsData.set(r.name, { count: r.count, unread: r.unread, preview: r.preview });
            });
            renderMyRoomList();
            return;
        }
        if (ev.data.startsWith("ROOMUPDATE|")) {
            // 필드: 방이름|안읽음수|인원수|메시지여부(1=진짜 채팅, 0=시스템
            // 알림)|메시지id|미리보기. isMsg가 1일 때만(진짜 채팅 메시지일
            // 때만) 화면 상단에 알람 팝업을 띄운다 - 입장/퇴장/방장변경 같은
            // 알림은 "내 채팅방" 목록의 미리보기 텍스트만 갱신하고 팝업은
            // 띄우지 않는다. msgId는 팝업을 눌러 그 방으로 넘어갔을 때 바로
            // 해당 메시지로 통통 튀며 이동하는 데 쓴다.
            const [, roomName, unreadStr, countStr, isMsgStr, msgIdStr, preview] = splitProtocol(ev.data, 7);
            const existing = myRoomsData.get(roomName) || { count: 0, preview: "" };
            myRoomsData.set(roomName, {
                count: countStr ? parseInt(countStr, 10) : existing.count,
                unread: parseInt(unreadStr, 10),
                preview: preview || existing.preview,
            });
            renderMyRoomList();
            if (isMsgStr === "1") showRoomAlertToast(roomName, msgIdStr, preview);
            return;
        }
        if (ev.data.startsWith("ACTIVE|")) {
            // 이미 참여 중인 방을 "내 채팅방" 목록/알람 팝업에서 열어본 경우.
            // 지금 다른 방을 보고 있던 중이었어도(inRoom===true) 그 화면을
            // 정리하고 곧바로 새 방으로 전환할 수 있어야 하므로, inRoom 여부와
            // 무관하게 항상 여기서 처리한다. 새로 입장하는 게 아니므로
            // "입장했습니다" 안내 없이 바로 채팅 화면을 보여준다 - 과거 대화/
            // 참가자/방장/뮤트/색상은 곧이어 오는 HISTORY/ROSTER/OWNER/MUTE/
            // COLORS로 채워진다.
            const [, roomName, name] = splitProtocol(ev.data, 3);
            if (roomName) {
                resetRoomState();
                inRoom = true;
                currentRoomName = roomName;
                username = name;
                chatRoomName.textContent = roomName;
                showScreen("chat");
            }
            // roomName이 빈 "ACTIVE|"는 backToLobbyBtn이 보낸 SWITCH_ROOM|에 대한
            // 응답인데, 그 버튼은 이미 낙관적으로 로컬 정리를 끝냈으므로 무시한다.
            return;
        }
        if (ev.data.startsWith("HISTORY_BEGIN|")) {
            return; // 시작 마커 자체는 그릴 게 없다 - 사이의 HISTMSG들을 위한 신호일 뿐.
        }
        if (ev.data.startsWith("HISTORY_END|")) {
            // 알람 팝업을 눌러서 이 방으로 넘어온 거라면, 과거 메시지가 전부
            // 그려진 지금이 그 메시지로 통통 튀며 이동할 수 있는 시점이다.
            const [, roomName] = splitProtocol(ev.data, 2);
            if (pendingAlertJump && pendingAlertJump.roomName === roomName && roomName === currentRoomName) {
                jumpToMessage(pendingAlertJump.msgId);
                pendingAlertJump = null;
            }
            return;
        }
        if (ev.data.startsWith("HISTMSG|")) {
            const [, roomName, id, sender, deletedFlag, editedFlag, isReplyFlag, origSender, origPreview,
                   isFileFlag, fileId, fileSizeStr, text] = splitProtocol(ev.data, 13);
            if (roomName === currentRoomName) {
                renderHistoryMessage(sender, id, text, deletedFlag === "1", editedFlag === "1",
                    isReplyFlag === "1", origSender, origPreview,
                    isFileFlag === "1", fileId, parseInt(fileSizeStr, 10));
            }
            return;
        }
        if (ev.data.startsWith("KICKED|")) {
            const [, roomName] = splitProtocol(ev.data, 2);
            myRoomsData.delete(roomName);
            if (roomName === currentRoomName) {
                alert("방장에 의해 추방되었습니다.");
                resetRoomState();
                backToLobbyScreen();
                ws.send("MYROOMS");
            } else {
                // 지금 보고 있지 않은 다른 방에서 추방된 것 - 조용히 목록에서만 뺀다.
                renderMyRoomList();
            }
            return;
        }

        if (!inRoom) {
            if (ev.data === "LOBBY") {
                showScreen("lobby");
                showLobbyTab(activeLobbyTab);
                ws.send("MYROOMS");
                ws.send("ROOMS");
                return;
            }
            if (ev.data.startsWith("ROOMLIST")) {
                renderRoomList(parseRoomList(ev.data));
                return;
            }
            if (ev.data.startsWith("ROOM_OK|")) {
                const [, roomName] = splitProtocol(ev.data, 2);
                pendingRoomName = roomName;
                nicknameEntryRoomName.textContent = roomName + " 방에 입장합니다.";
                nicknameError.textContent = "";
                roomUsernameInput.value = "";
                showScreen("nicknameEntry");
                roomUsernameInput.focus();
                return;
            }
            if (ev.data.startsWith("JOINED|")) {
                const [, roomName, name] = splitProtocol(ev.data, 3);
                username = name;
                currentRoomName = roomName;
                inRoom = true;
                chatRoomName.textContent = roomName;
                showScreen("chat");
                appendSystem(username + "님이 입장했습니다.");
                return;
            }
            if (ev.data.startsWith("ERR|")) {
                const [, msg] = splitProtocol(ev.data, 2);
                if (pendingAction === "create") createRoomError.textContent = msg;
                else if (pendingAction === "join") passwordError.textContent = msg;
                else if (pendingAction === "nickname") nicknameError.textContent = msg;
                return;
            }
            return;
        }

        // --- 여기부터는 지금 화면에 방을 하나 띄워놓고 보는 중일 때의 프로토콜 ---

        // ROSTER는 필드 개수가 가변("|"로 이어진 이름 목록)이라 splitProtocol의
        // 고정 maxParts 방식으로는 못 쪼갠다. 그냥 통째로 "|" 기준 split한다.
        // 맨 앞 두 토큰은 "ROSTER"와 방 이름이므로 건너뛴다.
        if (ev.data.startsWith("ROSTER|")) {
            ev.data.split("|").slice(2).filter(Boolean).forEach((name) => participants.add(name));
            return;
        }

        // COLORS는 입장/방 전환 직후 조용히 보내는 초기 동기화용 일괄 목록이다
        // ("이름|색상"이 반복). 실시간 변경 알림(COLOR, 단수형)과 달리 여기서는
        // 시스템 메시지를 띄우지 않는다.
        if (ev.data.startsWith("COLORS|")) {
            const tokens = ev.data.split("|").slice(2);
            for (let i = 0; i + 1 < tokens.length; i += 2) {
                userColors.set(tokens[i], tokens[i + 1]);
            }
            return;
        }

        // PINNED는 필드가 7개(roomName, id, sender, isFile, fileId, fileSize, text)라
        // 별도 maxParts로 분리. 방을 (재)활성화할 때마다 현재 고정 상태를
        // 조용히 재동기화하는 데도, 방장이 실시간으로 고정할 때도 똑같이 쓰인다.
        if (ev.data.startsWith("PINNED|")) {
            const [, roomName, id, sender, isFileFlag, fileId, fileSizeStr, text] = splitProtocol(ev.data, 8);
            if (roomName === currentRoomName) {
                pinnedMessage = { msgId: id, sender, isFile: isFileFlag === "1", fileId,
                    fileSize: parseInt(fileSizeStr, 10), text };
                renderPinnedPopup();
            }
            return;
        }
        if (ev.data.startsWith("UNPINNED|")) {
            const [, roomName] = splitProtocol(ev.data, 2);
            if (roomName === currentRoomName) {
                pinnedMessage = null;
                renderPinnedPopup();
            }
            return;
        }

        // WHISPER는 필드가 5개(roomName, from, to, text)라 별도 maxParts로 분리.
        // 지금 보고 있는 방이 아니면(백그라운드 방으로 온 귓속말이면) 그냥
        // 놓친다 - 이 앱은 원래 메시지 기록을 안 남기는 휘발성 설계다.
        if (ev.data.startsWith("WHISPER|")) {
            const [, roomName, from, to, text] = splitProtocol(ev.data, 5);
            if (roomName === currentRoomName) appendWhisper(from, to, text, false);
            return;
        }

        // REPLY는 필드가 6개(roomName, id, sender, origSender, origPreview, body)라 별도 maxParts로 분리.
        if (ev.data.startsWith("REPLY|")) {
            const [, roomName, id, sender, origSender, origPreview, body] = splitProtocol(ev.data, 7);
            if (roomName !== currentRoomName) return;
            if (blockedUsers.has(sender)) {
                appendSystem("[차단된 메시지] " + sender + "님의 메시지가 숨겨졌습니다.");
            } else {
                const msgDiv = appendReply(sender, origSender, origPreview, body, id);
                if (sender !== username) handleMentions(sender, body, msgDiv);
            }
            return;
        }

        // MSG도 방 이름이 붙어 필드가 4개(roomName, id, sender, text)이므로 별도
        // maxParts로 분리. 수정/삭제가 이 id로 메시지를 특정하기 때문에, 서버가
        // 보낸 사람 본인에게도 그대로 방송해준다(자기 메시지에도 id가 필요해서) -
        // 그래서 sender===username인 경우(내가 보낸 메시지가 되돌아온 것)는
        // 멘션 체크를 건너뛴다.
        if (ev.data.startsWith("MSG|")) {
            const [, roomName, id, sender, text] = splitProtocol(ev.data, 5);
            if (roomName !== currentRoomName) return;
            if (blockedUsers.has(sender)) {
                appendSystem("[차단된 메시지] " + sender + "님의 메시지가 숨겨졌습니다.");
            } else {
                const msgDiv = appendMessage(sender, text, id);
                if (sender !== username) handleMentions(sender, text, msgDiv);
            }
            return;
        }

        // FILE도 필드가 6개(roomName, id, sender, fileId, fileSize, fileName)라
        // 별도 maxParts로 분리. MSG와 마찬가지로 서버 echo를 기다렸다가 그린다
        // (id가 있어야 나중에 삭제를 요청할 수 있어서). 파일명에는 멘션이 낄
        // 일이 거의 없으므로 멘션 검사는 하지 않는다.
        if (ev.data.startsWith("FILE|")) {
            const [, roomName, id, sender, fileId, fileSizeStr, fileName] = splitProtocol(ev.data, 7);
            if (roomName !== currentRoomName) return;
            if (blockedUsers.has(sender)) {
                appendSystem("[차단된 메시지] " + sender + "님의 메시지가 숨겨졌습니다.");
            } else {
                appendFileMessage(sender, fileId, fileName, parseInt(fileSizeStr, 10), id);
            }
            return;
        }

        if (ev.data.startsWith("EDITED|")) {
            const [, roomName, msgId, newText] = splitProtocol(ev.data, 4);
            if (roomName === currentRoomName) applyEditedMessage(msgId, newText);
            return;
        }

        if (ev.data.startsWith("DELETED|")) {
            const [, roomName, msgId] = splitProtocol(ev.data, 3);
            if (roomName === currentRoomName) applyDeletedMessage(msgId);
            return;
        }

        // REACTIONS는 필드 개수가 가변("이모티콘|사용자" 쌍이 반복)이라
        // splitProtocol의 고정 maxParts로는 못 쪼갠다. ROSTER/COLORS와 같은
        // 방식으로 통째로 "|" 기준 split한 뒤 앞 세 토큰(REACTIONS/방이름/
        // msgId)을 건너뛰고 나머지를 둘씩 묶어 읽는다. 반응이 하나도 없으면
        // 남는 토큰이 없으므로 reactions는 빈 배열이 된다.
        if (ev.data.startsWith("REACTIONS|")) {
            const tokens = ev.data.split("|");
            const roomName = tokens[1];
            const msgId = tokens[2];
            if (roomName === currentRoomName) {
                const reactions = [];
                for (let i = 3; i + 1 < tokens.length; i += 2) {
                    reactions.push({ emoji: tokens[i], user: tokens[i + 1] });
                }
                applyReactions(msgId, reactions);
            }
            return;
        }

        // JOIN/LEAVE/KICK/RENAME/OWNER/MUTE/COLOR는 전부 "방이름|필드1|필드2"
        // 형태를 공유한다(필드가 하나뿐인 타입은 필드2가 빈 문자열로 온다).
        const [type, roomNameField, ...rest] = splitProtocol(ev.data, 4);
        if (roomNameField !== currentRoomName) return; // 지금 보고 있는 방이 아니면 무시(안전장치)

        if (type === "JOIN") {
            participants.add(rest[0]);
            appendSystem(rest[0] + "님이 입장했습니다.");
        } else if (type === "LEAVE") {
            participants.delete(rest[0]);
            appendSystem(rest[0] + "님이 나갔습니다.");
        } else if (type === "KICK") {
            participants.delete(rest[0]);
            appendSystem(rest[0] + "님이 방장에 의해 추방됐습니다.");
        } else if (type === "RENAME") {
            participants.delete(rest[0]);
            participants.add(rest[1]);
            appendSystem(rest[0] + "님이 " + rest[1] + "(으)로 닉네임을 변경했습니다.");
        } else if (type === "OWNER") {
            const name = rest[0];
            if (currentOwner !== null && currentOwner !== name) {
                appendSystem(name + "님이 방장으로 승급했습니다.");
            }
            currentOwner = name;
            // 고정 해제(✕) 버튼은 방장에게만 보이므로, 방장이 바뀌면 다시 그린다.
            if (pinnedMessage) renderPinnedPopup();
        } else if (type === "MUTE") {
            const name = rest[0];
            const seconds = parseInt(rest[1], 10);
            appendSystem(name + "님의 채팅이 " + seconds + "초 동안 제한되었습니다.");
            if (name === username) startMuteCountdown(seconds);
        } else if (type === "COLOR") {
            const name = rest[0];
            const colorName = rest[1];
            userColors.set(name, colorName);
            appendSystem(name + "님의 닉네임 색상이 " + colorName + "로 변경되었습니다.");
        } else if (type === "ERR") {
            appendSystem("오류: " + rest[0]);
        }
    };

    ws.onclose = () => {
        // 이제 강퇴는 연결을 안 끊으므로, 이 이벤트는 정말로 예기치 못한
        // 연결 끊김(브리지 프로세스가 죽는 등)일 때만 온다.
        alert("서버와의 연결이 끊겼습니다. 새로고침 해주세요.");
    };
}

// 방(화면에 띄워놓고 보던 방)을 벗어날 때(강퇴/자진 나가기/뒤로가기/다른
// 방으로 전환 공통) 이전 방의 흔적(참가자 목록/방장/뮤트 상태/로그/멘션
// 토스트)이 다음 화면에 남아있지 않도록 관련 상태를 초기화한다. 서버 쪽
// 멤버십 자체는 건드리지 않는다(그건 호출부가 필요에 따라 LEAVE_ROOM/
// SWITCH_ROOM으로 따로 처리한다).
function resetRoomState() {
    inRoom = false;
    currentRoomName = null;

    participants.clear();
    userColors.clear();
    blockedUsers.clear();
    currentOwner = null;
    pinnedMessage = null;
    renderPinnedPopup();
    lastWhisperPartner = null;
    if (muteTimerId !== null) { clearInterval(muteTimerId); muteTimerId = null; }
    muteRemaining = 0;
    clearMuteUi();
    logDiv.innerHTML = "";
    mentionToasts.innerHTML = "";
    lastDividerKey = null;
    groupUser = null;
    groupLastTime = null;
    groupLastTimeSpan = null;
    currentGroupDiv = null;
    cancelReply();
    editTarget = null;
    closeMessageContextMenu();
    closeRoomContextMenu();
    updateScrollToBottomBtn();
}

// "/w 대상 메시지" 형태만 지원한다. "/"로 시작하는데 이 형식에 안 맞거나
// 대상이 지금 방에 없으면 알 수 없는 명령어로 취급한다.
const WHISPER_RE = /^\/w\s+(\S+)\s+(.+)$/;
const KICK_RE = /^\/kick\s+(\S+)$/;
const MUTE_RE = /^\/mute\s+(\S+)\s+(\d+)$/;
const BLOCK_RE = /^\/block\s+(\S+)$/;
const UNBLOCK_RE = /^\/unblock\s+(\S+)$/;
const BLOCKLIST_RE = /^\/blocklist$/i;

// "/help"가 보여줄 명령어 목록의 단일 출처. 새 명령어를 추가할 때는 항상
// 여기에도 한 줄 추가한다 - 이 배열에 없는 명령어는 "/help"에 안 뜬다.
const COMMANDS = [
    { usage: "/w [닉네임] [내용]", desc: "해당 유저에게 귓속말을 보냅니다." },
    { usage: "/r [내용]", desc: "가장 최근 귓속말 상대에게 답장합니다." },
    { usage: "/kick [닉네임]", desc: "(방장 전용) 해당 유저를 채팅방에서 추방합니다." },
    { usage: "/mute [닉네임] [초]", desc: "(방장 전용) 해당 유저의 채팅을 지정한 시간(초) 동안 제한합니다." },
    { usage: "@닉네임", desc: "메시지 중간에 넣으면 해당 유저를 멘션합니다(팝업 알림이 뜹니다)." },
    { usage: "@all", desc: "(방장 전용) 나를 제외한 모든 사람을 멘션합니다." },
    { usage: "/block [닉네임]", desc: "해당 유저가 보낸 메시지를 화면에서 숨깁니다." },
    { usage: "/unblock [닉네임]", desc: "해당 유저의 메시지 숨김을 해제합니다." },
    { usage: "/blocklist", desc: "현재 차단 중인 유저 목록을 보여줍니다." },
    { usage: "/help", desc: "사용 가능한 명령어 목록을 보여줍니다." },
];

// 뮤트 카운트다운: 1초씩 줄여가며 입력창을 잠그고, 남은 시간을 placeholder에
// 보여준다. 0이 되면 자동으로 풀린다. 서버도 별도로 뮤트 여부를 검사하므로
// 이 UI는 어디까지나 사용자 경험용이고, 실제 제한은 서버가 강제한다.
function applyMuteUi() {
    msgInput.disabled = true;
    sendBtn.disabled = true;
    msgInput.placeholder = muteRemaining + "초 동안 채팅이 제한되었습니다";
}

function clearMuteUi() {
    msgInput.disabled = false;
    sendBtn.disabled = false;
    msgInput.placeholder = "메시지를 입력하세요";
}

function startMuteCountdown(seconds) {
    if (muteTimerId !== null) clearInterval(muteTimerId);
    muteRemaining = seconds;
    applyMuteUi();
    muteTimerId = setInterval(() => {
        muteRemaining--;
        if (muteRemaining <= 0) {
            clearInterval(muteTimerId);
            muteTimerId = null;
            muteRemaining = 0;
            clearMuteUi();
        } else {
            applyMuteUi();
        }
    }, 1000);
}

function sendMessage() {
    const raw = msgInput.value;
    if (!raw.trim()) return;
    closeMentionPopup();

    if (editTarget) {
        // 수정 모드에서는 입력한 내용을 명령어로 해석하지 않고 그대로 새
        // 내용으로 보낸다 - 원래 메시지도 평범한 텍스트였으므로.
        ws.send("EDIT|" + currentRoomName + "|" + editTarget.id + "|" + raw);
        editTarget = null;
        msgInput.value = "";
        return;
    }

    if (raw.startsWith("/")) {
        if (raw.trim() === "/help") {
            appendSystem("사용 가능한 명령어:");
            COMMANDS.forEach((c) => appendSystem(c.usage + " - " + c.desc));
            msgInput.value = "";
            return;
        }

        // /kick, /mute, /block, /unblock, /blocklist는 자유 텍스트 본문이 없는
        // 명령어라서 정규식이 문자열 끝을 딱 맞춰 요구한다. 그런데 팝업에서
        // 닉네임을 고르면 selectMention이 다음 인자를 이어 칠 수 있도록 이름
        // 뒤에 공백을 하나 남겨두므로, 그 상태 그대로 바로 Enter를 치면(=/mute는
        // 초까지 다 친 뒤에도) 끝에 공백이 남아 매칭이 깨진다. 이 명령어들만
        // trim한 문자열로 매칭한다 - /w는 뒤에 자유 텍스트 본문이 있으므로
        // 원본 그대로 써야 메시지의 공백이 안 잘린다.
        const cmd = raw.trim();

        const kickM = KICK_RE.exec(cmd);
        if (kickM) {
            if (currentOwner !== username) {
                appendSystem("방장만 사용할 수 있는 명령어입니다.");
            } else {
                ws.send("KICK|" + currentRoomName + "|" + kickM[1]);
            }
            msgInput.value = "";
            return;
        }

        const muteM = MUTE_RE.exec(cmd);
        if (muteM) {
            if (currentOwner !== username) {
                appendSystem("방장만 사용할 수 있는 명령어입니다.");
            } else {
                ws.send("MUTE|" + currentRoomName + "|" + muteM[1] + "|" + muteM[2]);
            }
            msgInput.value = "";
            return;
        }

        // 차단은 서버가 알 필요 없는, 이 브라우저만의 로컬 설정이다 - 그냥
        // blockedUsers에 넣고 뺄 뿐, 아무 것도 전송하지 않는다.
        const blockM = BLOCK_RE.exec(cmd);
        if (blockM) {
            blockedUsers.add(blockM[1]);
            appendSystem(blockM[1] + "님의 메시지가 숨겨졌습니다.");
            msgInput.value = "";
            return;
        }

        const unblockM = UNBLOCK_RE.exec(cmd);
        if (unblockM) {
            blockedUsers.delete(unblockM[1]);
            appendSystem(unblockM[1] + "님의 메시지가 숨김 해제됐습니다.");
            msgInput.value = "";
            return;
        }

        if (BLOCKLIST_RE.test(cmd)) {
            appendSystem(
                blockedUsers.size === 0
                    ? "차단한 사람이 없습니다."
                    : "차단 목록: " + [...blockedUsers].sort().join(", ")
            );
            msgInput.value = "";
            return;
        }

        const m = WHISPER_RE.exec(raw);
        const target = m ? m[1] : null;
        if (!m || !participants.has(target)) {
            appendSystem("해당 명령어를 찾을 수 없습니다.");
            msgInput.value = "";
            return;
        }
        const whisperText = m[2];
        ws.send("WHISPER|" + currentRoomName + "|" + target + "|" + whisperText);
        appendWhisper(username, target, whisperText, true);
        msgInput.value = "";
        return;
    }

    if (replyTarget) {
        const preview = buildReplyPreview(replyTarget.text);
        const origSender = replyTarget.sender;
        // 렌더링은 여기서 직접 하지 않는다 - 서버가 매긴 메시지 id가 있어야
        // 나중에 수정/삭제를 할 수 있으므로, 서버가 나에게도 그대로 되돌려주는
        // REPLY 방송을 받은 뒤에 ws.onmessage 쪽에서 그린다.
        ws.send("REPLY|" + currentRoomName + "|" + origSender + "|" + preview + "|" + raw);
        cancelReply(); // 답장은 1회성이라 보내고 나면 모드가 자동으로 꺼진다
        msgInput.value = "";
        return;
    }

    // MSG도 마찬가지로 로컬에서 먼저 그리지 않는다 - 서버가 매긴 id가 필요해서
    // 서버의 되돌아온 방송을 기다렸다가 ws.onmessage에서 그린다.
    ws.send("MSG|" + currentRoomName + "|" + raw);
    msgInput.value = "";
}

sendBtn.addEventListener("click", sendMessage);

// --- 귓속말/추방/뮤트/차단/멘션 대상 자동완성 팝업 ---
// "/w", "/kick", "/mute", "/block", "/unblock" 중 하나까지만 치면 전원, 뒤에
// 부분이름을 치면 그 부분 문자열을 포함하는 사람만 남기고, 하나도 안 맞으면
// 팝업을 닫는다.
// "@"는 이것들과 달리 입력창 맨 앞이 아니라 캐럿 바로 앞에서, 메시지 중간
// 어디서든 트리거된다(예: "안녕 @밥"). 스페이스바는 채팅 내용에도 쓰이므로,
// 화살표로 목록을 탐색하기 시작했거나 마우스로 클릭한 경우가 아니면
// 스페이스바로는 선택되지 않는다(그냥 평범한 공백으로 입력됨).
let popupCommand = null;
let popupNames = [];
let popupHighlight = -1;
let popupNavActive = false;
// "@" 모드에서만 쓴다: 입력값 중 "@부분입력" 구간의 [시작, 끝) 인덱스.
// null이면 슬래시 명령 모드(입력창 전체를 "/명령 이름 "으로 치환).
let popupAtStart = null;
let popupAtEnd = null;

function isMentionPopupOpen() {
    return !mentionPopup.classList.contains("hidden");
}

function closeMentionPopup() {
    mentionPopup.classList.add("hidden");
    mentionPopup.innerHTML = "";
    popupCommand = null;
    popupNames = [];
    popupHighlight = -1;
    popupNavActive = false;
    popupAtStart = null;
    popupAtEnd = null;
}

function renderMentionPopup() {
    mentionPopup.innerHTML = "";
    popupNames.forEach((name, idx) => {
        const item = document.createElement("div");
        item.className = "mention-item" + (idx === popupHighlight ? " highlighted" : "");
        if (name === "all") {
            item.textContent = "@all (전체 멘션)";
            item.classList.add("mention-item-all");
        } else {
            styleNameLabel(item, name);
        }
        item.addEventListener("click", () => selectMention(idx));
        mentionPopup.appendChild(item);
    });
}

function openMentionPopup(command, names, atStart, atEnd) {
    popupCommand = command;
    popupAtStart = (atStart !== undefined) ? atStart : null;
    popupAtEnd = (atEnd !== undefined) ? atEnd : null;
    popupNames = names;
    popupHighlight = -1;
    popupNavActive = false;
    mentionPopup.classList.remove("hidden");
    renderMentionPopup();
}

function selectMention(idx) {
    const name = popupNames[idx];
    if (name === undefined) return;

    if (popupAtStart !== null) {
        // "@부분입력" 구간만 "@완전한이름 "으로 치환하고, 앞뒤 텍스트는 그대로 둔다.
        const before = msgInput.value.slice(0, popupAtStart);
        const after = msgInput.value.slice(popupAtEnd);
        const inserted = "@" + name + " ";
        msgInput.value = before + inserted + after;
        const caret = before.length + inserted.length;
        closeMentionPopup();
        msgInput.focus();
        msgInput.setSelectionRange(caret, caret);
        return;
    }

    msgInput.value = "/" + popupCommand + " " + name + " ";
    closeMentionPopup();
    msgInput.focus();
    const caret = msgInput.value.length;
    msgInput.setSelectionRange(caret, caret);
}

function moveMentionHighlight(delta) {
    if (popupNames.length === 0) return;
    popupHighlight = (popupHighlight + delta + popupNames.length) % popupNames.length;
    renderMentionPopup();
}

// 아직 "/w|kick|mute|block|unblock [부분이름]"까지만 입력된 상태에서만 팝업을 띄운다.
// 대상 이름 뒤에 진짜 공백을 쳐서 메시지 본문(/w)이나 초(/mute)를 쓰기
// 시작하면(=두 번째 공백) 더 이상 대상을 고르는 단계가 아니므로 팝업을 닫는다.
const MENTION_TRIGGER_RE = /^\/(w|kick|mute|block|unblock)(?: (\S*))?$/;
// 캐럿 바로 앞이 "(줄 시작 또는 공백)@부분입력"으로 끝나는지 검사한다.
// "abc@x" 같은(공백 없이 붙은) 건 멘션으로 보지 않는다(이메일 등 오탐 방지).
const AT_TRIGGER_RE = /(^|\s)@(\S*)$/;

function updateMentionPopup() {
    const caret = msgInput.selectionStart;
    const beforeCaret = msgInput.value.slice(0, caret);
    const atMatch = AT_TRIGGER_RE.exec(beforeCaret);

    if (atMatch) {
        const query = atMatch[2].toLowerCase();
        const atStart = caret - atMatch[2].length - 1; // "@"의 인덱스
        const names = [...participants]
            .filter((name) => name.toLowerCase().includes(query))
            .sort();
        // @all은 방장에게만 보이고, 항상 목록 맨 앞에 둔다(알파벳 순서에
        // 섞이면 눈에 덜 띈다).
        if (currentOwner === username && "all".includes(query)) names.unshift("all");
        if (names.length === 0) {
            closeMentionPopup();
            return;
        }
        openMentionPopup("at", names, atStart, caret);
        return;
    }

    const match = MENTION_TRIGGER_RE.exec(msgInput.value);
    if (!match) {
        closeMentionPopup();
        return;
    }
    const query = (match[2] || "").toLowerCase();
    const names = [...participants]
        .filter((name) => name.toLowerCase().includes(query))
        .sort();
    if (names.length === 0) {
        closeMentionPopup();
        return;
    }
    openMentionPopup(match[1], names);
}

// "/r"만 치면 가장 최근 귓속말 상대에게 "/w 이름 "을 자동으로 채워준다.
function expandReplyShortcut() {
    if (!lastWhisperPartner) return false;
    if (msgInput.value !== "/r" && msgInput.value !== "/r ") return false;
    msgInput.value = "/w " + lastWhisperPartner + " ";
    const caret = msgInput.value.length;
    msgInput.setSelectionRange(caret, caret);
    return true;
}

msgInput.addEventListener("input", () => {
    popupNavActive = false;
    if (expandReplyShortcut()) return;
    updateMentionPopup();
});

msgInput.addEventListener("keydown", (e) => {
    if (isMentionPopupOpen()) {
        if (e.key === "ArrowDown") {
            e.preventDefault();
            popupNavActive = true;
            moveMentionHighlight(1);
            return;
        }
        if (e.key === "ArrowUp") {
            e.preventDefault();
            popupNavActive = true;
            moveMentionHighlight(-1);
            return;
        }
        if ((e.key === " " || e.key === "Enter") && popupNavActive && popupHighlight >= 0) {
            e.preventDefault();
            selectMention(popupHighlight);
            return;
        }
        if (e.key === "Escape") {
            e.preventDefault();
            closeMentionPopup();
            return;
        }
    }

    // 프로토콜의 한 줄(line)이 메시지 하나와 대응해야 하므로, textarea 기본 동작인
    // 줄바꿈 삽입은 막고 그대로 전송한다.
    if (e.key === "Enter") {
        e.preventDefault();
        sendMessage();
    }
});

// --- 이모티콘 패널 ---

EMOJIS.forEach((emoji) => {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.textContent = emoji;
    btn.addEventListener("click", () => insertEmoji(emoji));
    emojiPanel.appendChild(btn);
});

function insertEmoji(emoji) {
    const start = msgInput.selectionStart ?? msgInput.value.length;
    const end = msgInput.selectionEnd ?? msgInput.value.length;
    msgInput.value = msgInput.value.slice(0, start) + emoji + msgInput.value.slice(end);
    msgInput.focus();
    const caret = start + emoji.length;
    msgInput.setSelectionRange(caret, caret);
}

emojiBtn.addEventListener("click", (e) => {
    e.stopPropagation();
    colorPalette.classList.add("hidden");
    emojiPanel.classList.toggle("hidden");
});

document.addEventListener("click", (e) => {
    if (emojiPanel.classList.contains("hidden")) return;
    if (e.target === emojiBtn || emojiPanel.contains(e.target)) return;
    emojiPanel.classList.add("hidden");
});

document.addEventListener("click", (e) => {
    if (!isMentionPopupOpen()) return;
    if (e.target === msgInput || mentionPopup.contains(e.target)) return;
    closeMentionPopup();
});

// --- 닉네임 색상 팔레트 ---
// 이모티콘 패널과 같은 자리에서 같은 방식으로 토글되지만, 스와치를 고르면
// 바로 서버에 COLOR 명령을 보내 내 닉네임 색을 바꾼다(전송할 메시지가 아님).

Object.keys(COLOR_HEX).forEach((colorName) => {
    const swatch = document.createElement("button");
    swatch.type = "button";
    swatch.className = "color-swatch";
    swatch.style.background = COLOR_HEX[colorName];
    swatch.title = colorName;
    swatch.addEventListener("click", () => {
        ws.send("COLOR|" + currentRoomName + "|" + colorName);
        colorPalette.classList.add("hidden");
    });
    colorPalette.appendChild(swatch);
});

paletteBtn.addEventListener("click", (e) => {
    e.stopPropagation();
    emojiPanel.classList.add("hidden");
    colorPalette.classList.toggle("hidden");
});

document.addEventListener("click", (e) => {
    if (colorPalette.classList.contains("hidden")) return;
    if (e.target === paletteBtn || colorPalette.contains(e.target)) return;
    colorPalette.classList.add("hidden");
});

// --- 파일 드래그 앤 드롭 업로드 ---
// 서버(chat_client)의 MAX_UPLOAD_BYTES와 일치해야 한다 - 여기서 먼저
// 걸러내면 큰 파일을 굳이 다 전송하고 나서야 거절당하는 걸 피할 수 있다.
const MAX_UPLOAD_BYTES = 20 * 1024 * 1024;

// dragenter/dragleave는 로그 안의 자식 요소를 오갈 때마다 쌍으로 계속
// 발생하므로(예: 말풍선 위로 지나갈 때), 카운터로 세어서 진짜로 로그
// 영역을 완전히 벗어났을 때만 오버레이를 지운다.
let dragCounter = 0;

logWrapperDiv.addEventListener("dragover", (e) => {
    if (!inRoom) return;
    e.preventDefault();
});
logWrapperDiv.addEventListener("dragenter", (e) => {
    if (!inRoom) return;
    e.preventDefault();
    dragCounter++;
    dropOverlay.classList.remove("hidden");
});
logWrapperDiv.addEventListener("dragleave", () => {
    if (!inRoom) return;
    dragCounter = Math.max(0, dragCounter - 1);
    if (dragCounter === 0) dropOverlay.classList.add("hidden");
});
logWrapperDiv.addEventListener("drop", (e) => {
    if (!inRoom) return;
    e.preventDefault();
    dragCounter = 0;
    dropOverlay.classList.add("hidden");
    const files = [...(e.dataTransfer?.files || [])];
    files.forEach(uploadFile);
});

// 업로드 자체는 REPLY/MSG처럼 낙관적으로 먼저 그리지 않는다 - 성공하면
// 서버가 FILE을 방송해오고, 그걸 받았을 때 비로소 말풍선을 그린다.
function uploadFile(file) {
    if (!inRoom) return;
    if (file.size > MAX_UPLOAD_BYTES) {
        appendSystem(file.name + " 파일이 너무 큽니다(최대 20MB).");
        return;
    }
    const url = "/upload?room=" + encodeURIComponent(currentRoomName) + "&filename=" + encodeURIComponent(file.name);
    fetch(url, { method: "POST", body: file })
        .then((res) => {
            if (res.ok) return;
            return res.text().then((msg) => {
                throw new Error(msg || ("업로드 실패 (" + res.status + ")"));
            });
        })
        .catch((err) => {
            appendSystem(file.name + " 업로드에 실패했습니다: " + err.message);
        });
}

// 페이지가 열리자마자 서버에 붙어 로비 상태(참여 중인 방이 있으면 "내
// 채팅방" 탭에서 바로 보인다)를 받아온다 - 예전처럼 닉네임부터 입력받고
// 나서 붙는 방식이 아니다.
connect();
