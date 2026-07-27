#include "i18n.h"

#include <windows.h>

namespace cpulytics {
namespace {

enum { kEn, kEs, kRu, kZh, kJa, kKo, kAr, kLangCount };

const wchar_t* const kCodes[] = {L"auto", L"en", L"es", L"ru", L"zh", L"ja", L"ko", L"ar", nullptr};
const wchar_t* const kNames[] = {L"Auto", L"English", L"Español", L"Русский", L"中文", L"日本語", L"한국어",
                                 L"العربية"};

// Rows are in the order of the Str enum. Columns: en, es, ru, zh, ja, ko, ar.
const wchar_t* const kText[S_COUNT][kLangCount] = {
    // chrome
    {L"cpulytics settings", L"Configuración de cpulytics", L"Настройки cpulytics", L"cpulytics 设置",
     L"cpulytics の設定", L"cpulytics 설정", L"إعدادات cpulytics"},
    {L"Language", L"Idioma", L"Язык", L"语言", L"言語", L"언어", L"اللغة"},
    {L"Save", L"Guardar", L"Сохранить", L"保存", L"保存", L"저장", L"حفظ"},
    {L"Cancel", L"Cancelar", L"Отмена", L"取消", L"キャンセル", L"취소", L"إلغاء"},
    {L"Defaults", L"Predeterminados", L"По умолчанию", L"默认值", L"既定値", L"기본값", L"الافتراضي"},
    {L"Range", L"Rango", L"Диапазон", L"范围", L"範囲", L"범위", L"النطاق"},
    {L"default", L"predeterminado", L"по умолчанию", L"默认", L"既定", L"기본", L"افتراضي"},
    // tray
    {L"Managing priorities", L"Gestionando prioridades", L"Управление приоритетами", L"管理优先级",
     L"優先度を管理", L"우선순위 관리", L"إدارة الأولويات"},
    {L"Restore all now", L"Restaurar todo ahora", L"Восстановить все", L"立即恢复全部", L"すべて元に戻す",
     L"모두 되돌리기", L"استعادة الكل الآن"},
    {L"Settings...", L"Configuración...", L"Настройки...", L"设置...", L"設定...", L"설정...", L"الإعدادات..."},
    {L"Reload settings file", L"Recargar el archivo", L"Перечитать файл настроек", L"重新加载配置文件",
     L"設定ファイルを再読み込み", L"설정 파일 다시 읽기", L"إعادة تحميل ملف الإعدادات"},
    {L"Restart as administrator", L"Reiniciar como administrador", L"Перезапустить от администратора",
     L"以管理员身份重启", L"管理者として再起動", L"관리자 권한으로 다시 시작", L"إعادة التشغيل كمسؤول"},
    {L"Open log", L"Abrir el registro", L"Открыть журнал", L"打开日志", L"ログを開く", L"로그 열기", L"فتح السجل"},
    {L"Exit", L"Salir", L"Выход", L"退出", L"終了", L"종료", L"خروج"},
    {L"administrator", L"administrador", L"администратор", L"管理员", L"管理者", L"관리자", L"مسؤول"},
    {L"paused", L"en pausa", L"пауза", L"已暂停", L"一時停止", L"일시 중지", L"متوقف"},
    {L"top", L"max", L"макс", L"最高", L"最大", L"최대", L"الأعلى"},
    {L"lowered", L"bajado", L"понижен", L"已降低", L"引き下げ", L"낮춤", L"مخفض"},
    {L"idle", L"inactivo", L"простой", L"空闲", L"アイドル", L"유휴", L"خامل"},
    {L"fullscreen", L"pantalla completa", L"полный экран", L"全屏", L"全画面", L"전체 화면", L"ملء الشاشة"},
    {L"cpulytics: priority lowered", L"cpulytics: prioridad bajada", L"cpulytics: приоритет понижен",
     L"cpulytics：已降低优先级", L"cpulytics: 優先度を下げました", L"cpulytics: 우선순위를 낮췄습니다",
     L"cpulytics: تم خفض الأولوية"},
    {L"cpulytics: priority restored", L"cpulytics: prioridad restaurada", L"cpulytics: приоритет восстановлен",
     L"cpulytics：已恢复优先级", L"cpulytics: 優先度を戻しました", L"cpulytics: 우선순위를 복원했습니다",
     L"cpulytics: تمت استعادة الأولوية"},
    {L"and %u more", L"y %u más", L"и ещё %u", L"还有 %u 个", L"他 %u 件", L"외 %u개", L"و %u أخرى"},

    // settings rows: label then hint
    {L"Manage priorities", L"Gestionar prioridades", L"Управлять приоритетами", L"管理优先级", L"優先度を管理する",
     L"우선순위 관리", L"إدارة الأولويات"},
    {L"master switch", L"interruptor principal", L"главный переключатель", L"总开关", L"メインスイッチ",
     L"기본 스위치", L"المفتاح الرئيسي"},

    {L"Sample interval", L"Intervalo de muestreo", L"Интервал опроса", L"采样间隔", L"サンプリング間隔",
     L"샘플링 간격", L"فترة أخذ العينات"},
    {L"ms between reads of the process table", L"ms entre lecturas de la tabla de procesos",
     L"мс между чтениями списка процессов", L"读取进程表的间隔（毫秒）", L"プロセス一覧を読む間隔 (ミリ秒)",
     L"프로세스 목록을 읽는 간격 (밀리초)", L"مللي ثانية بين قراءات قائمة العمليات"},

    {L"Window", L"Ventana", L"Окно", L"统计窗口", L"ウィンドウ", L"관측 구간", L"النافذة"},
    {L"seconds of history the average is taken over", L"segundos de historial promediados",
     L"секунд истории для усреднения", L"用于求平均的历史秒数", L"平均を取る履歴の秒数",
     L"평균을 내는 기록 길이 (초)", L"ثواني السجل التي يحسب متوسطها"},

    {L"Minimum history", L"Historial mínimo", L"Минимум истории", L"最少历史", L"最小履歴", L"최소 기록",
     L"أقل سجل"},
    {L"seconds of data before anything is decided", L"segundos de datos antes de decidir",
     L"секунд данных до первого решения", L"作出判断前所需的数据秒数", L"判断を始めるまでに必要な秒数",
     L"판단을 시작하기 전 필요한 초", L"ثواني البيانات قبل اتخاذ أي قرار"},

    {L"Demote above", L"Bajar por encima de", L"Понижать выше", L"超过则降级", L"この値を超えたら降格",
     L"이 값을 넘으면 강등", L"خفض عند تجاوز"},
    {L"% of all cores, averaged over the window", L"% de todos los núcleos, promedio de la ventana",
     L"% всех ядер, среднее по окну", L"占全部核心的百分比（窗口平均）", L"全コアに対する % (ウィンドウ平均)",
     L"전체 코어 대비 % (구간 평균)", L"% من كل الأنوية، متوسط النافذة"},

    {L"Restore below", L"Restaurar por debajo de", L"Возвращать ниже", L"低于则恢复", L"この値を下回れば復帰",
     L"이 값 아래면 복원", L"استعادة عند أقل من"},
    {L"% of all cores, must stay under the demote level", L"% de todos los núcleos, por debajo del umbral",
     L"% всех ядер, ниже порога понижения", L"占全部核心的百分比，需低于降级阈值",
     L"全コアに対する %、降格しきい値未満", L"전체 코어 대비 %, 강등 기준보다 낮아야 함",
     L"% من كل الأنوية، أقل من حد الخفض"},

    {L"Startup grace", L"Margen de arranque", L"Пауза после старта", L"启动宽限", L"起動直後の猶予",
     L"시작 유예", L"مهلة البدء"},
    {L"seconds a freshly started process is left alone", L"segundos de gracia para un proceso recién iniciado",
     L"секунд не трогать только что запущенный процесс", L"新启动的进程免受影响的秒数",
     L"起動直後のプロセスを放置する秒数", L"막 시작한 프로세스를 두는 시간 (초)",
     L"ثواني تترك فيها العملية المبتدئة"},

    {L"Cooldown", L"Enfriamiento", L"Задержка между шагами", L"冷却时间", L"クールダウン", L"쿨다운",
     L"فترة التهدئة"},
    {L"seconds between two changes of one process", L"segundos entre dos cambios del mismo proceso",
     L"секунд между изменениями одного процесса", L"同一进程两次调整之间的秒数",
     L"同じプロセスを続けて変更しない秒数", L"같은 프로세스를 다시 바꾸기까지의 초",
     L"ثواني بين تغييرين لنفس العملية"},

    {L"Calm for", L"Tranquilo durante", L"Спокоен в течение", L"保持安静", L"静かな時間", L"안정 유지",
     L"هدوء لمدة"},
    {L"seconds of quiet before a step is given back", L"segundos en calma antes de devolver un paso",
     L"секунд тишины до возврата шага", L"归还一级前需保持安静的秒数", L"1 段階戻すまでに静かでいる秒数",
     L"한 단계 되돌리기까지 조용해야 하는 초", L"ثواني هدوء قبل إعادة درجة"},

    {L"Steps down", L"Pasos de bajada", L"Шагов понижения", L"降级级数", L"下げる段数", L"낮출 단계",
     L"درجات الخفض"},
    {L"0 off, 1 below normal, 2 down to idle", L"0 desactivado, 1 por debajo de normal, 2 hasta inactivo",
     L"0 выкл, 1 ниже среднего, 2 до простоя", L"0 关闭，1 低于正常，2 直到空闲", L"0 無効, 1 通常以下, 2 アイドルまで",
     L"0 끔, 1 보통 이하, 2 유휴까지", L"0 معطل، 1 أقل من عادي، 2 حتى الخامل"},

    {L"Steps for system", L"Pasos para el sistema", L"Шагов для системных", L"系统进程级数", L"システム用の段数",
     L"시스템 단계", L"درجات النظام"},
    {L"same cap for session 0 processes", L"mismo límite para procesos de la sesión 0",
     L"тот же предел для процессов сессии 0", L"会话 0 进程的上限", L"セッション 0 のプロセスの上限",
     L"세션 0 프로세스의 상한", L"نفس الحد لعمليات الجلسة 0"},

    {L"Steps for fullscreen", L"Pasos en pantalla completa", L"Шагов для полноэкранных", L"全屏程序级数",
     L"全画面アプリの段数", L"전체 화면 단계", L"درجات ملء الشاشة"},
    {L"games and players: 0 leaves them untouched", L"juegos y reproductores: 0 no los toca",
     L"игры и плееры: 0 - не трогать", L"游戏和播放器：0 表示不动它们", L"ゲームや動画: 0 なら触らない",
     L"게임과 플레이어: 0이면 건드리지 않음", L"الألعاب والمشغلات: 0 يعني عدم المساس"},

    {L"Protect foreground", L"Proteger el primer plano", L"Защищать активное окно", L"保护前台窗口",
     L"前面のアプリを保護", L"전면 창 보호", L"حماية النافذة النشطة"},
    {L"never demote the window you are using", L"nunca bajar la ventana que estás usando",
     L"не понижать окно, в котором работаешь", L"绝不降级正在使用的窗口", L"使用中のウィンドウは下げない",
     L"사용 중인 창은 낮추지 않음", L"لا تخفض النافذة التي تستخدمها"},

    {L"Notifications", L"Notificaciones", L"Уведомления", L"通知", L"通知", L"알림", L"الإشعارات"},
    {L"balloon on every change", L"aviso en cada cambio", L"всплывающее окно при каждом изменении",
     L"每次调整都弹出提示", L"変更のたびにバルーンを表示", L"변경할 때마다 풍선 알림", L"تنبيه عند كل تغيير"},

    {L"Restore on exit", L"Restaurar al salir", L"Восстанавливать при выходе", L"退出时恢复", L"終了時に復元",
     L"종료 시 복원", L"استعادة عند الخروج"},
    {L"put everything back when cpulytics stops", L"devolver todo al cerrar cpulytics",
     L"вернуть всё при остановке cpulytics", L"cpulytics 停止时还原全部", L"cpulytics 終了時にすべて戻す",
     L"cpulytics가 멈출 때 모두 되돌림", L"إرجاع كل شيء عند إيقاف cpulytics"},

    {L"Write log", L"Escribir registro", L"Вести журнал", L"写入日志", L"ログを書く", L"로그 기록",
     L"كتابة السجل"},
    {L"cpulytics.log next to the settings file", L"cpulytics.log junto al archivo de configuracion",
     L"cpulytics.log рядом с файлом настроек", L"cpulytics.log 与配置文件同目录",
     L"設定ファイルと同じ場所の cpulytics.log", L"설정 파일 옆의 cpulytics.log",
     L"cpulytics.log بجوار ملف الإعدادات"},

    {L"Max tracked", L"Máx. seguidos", L"Максимум процессов", L"最多跟踪", L"追跡上限", L"최대 추적 수",
     L"أقصى عدد متتبع"},
    {L"upper bound on the history map", L"límite superior del historial", L"верхняя граница карты истории",
     L"历史表的上限", L"履歴テーブルの上限", L"기록 테이블의 상한", L"الحد الأعلى لجدول السجل"},

    {L"Log size", L"Tamaño del registro", L"Размер журнала", L"日志大小", L"ログサイズ", L"로그 크기",
     L"حجم السجل"},
    {L"KB, the log is truncated past this", L"KB, el registro se trunca al superarlo",
     L"КБ, при превышении журнал обрезается", L"KB，超过后截断日志", L"KB、超えるとログを切り詰める",
     L"KB, 넘으면 로그를 잘라냄", L"ك.ب، يقطع السجل عند تجاوزه"},

    {L"Never touch", L"Nunca tocar", L"Никогда не трогать", L"永不干预", L"対象外", L"제외 목록",
     L"لا تلمس أبدا"},
    {L"executable names, comma separated", L"nombres de ejecutables separados por comas",
     L"имена программ через запятую", L"可执行文件名，用逗号分隔", L"実行ファイル名をカンマ区切りで",
     L"실행 파일 이름, 쉼표로 구분", L"أسماء الملفات التنفيذية مفصولة بفواصل"},

    {L"Efficiency mode", L"Modo de eficiencia", L"Режим энергоэффективности", L"能效模式", L"効率モード",
     L"효율 모드", L"وضع الكفاءة"},
    {L"also mark demoted processes as low power (EcoQoS)",
     L"marcar también los procesos bajados como de bajo consumo (EcoQoS)",
     L"помечать понижённые процессы как энергосберегающие (EcoQoS)", L"同时将降级进程标记为低功耗 (EcoQoS)",
     L"降格したプロセスを低電力 (EcoQoS) にする", L"강등된 프로세스를 저전력(EcoQoS)으로 표시",
     L"وسم العمليات المخفضة كمنخفضة الطاقة (EcoQoS)"},

    {L"cpulytics is already running", L"cpulytics ya se está ejecutando", L"cpulytics уже запущен",
     L"cpulytics 已在运行", L"cpulytics はすでに実行中です", L"cpulytics이(가) 이미 실행 중입니다",
     L"cpulytics يعمل بالفعل"},

    {L"Start with Windows", L"Iniciar con Windows", L"Запускать вместе с Windows", L"开机自启",
     L"Windows と一緒に起動", L"윈도우 시작 시 실행", L"التشغيل مع ويندوز"},
    {L"run cpulytics at logon, for this user", L"ejecutar cpulytics al iniciar sesión",
     L"запускать cpulytics при входе в систему", L"登录时启动 cpulytics", L"サインイン時に cpulytics を起動",
     L"로그인할 때 cpulytics 실행", L"تشغيل cpulytics عند تسجيل الدخول"},

    {L"this cpu has no efficiency cores", L"esta cpu no tiene núcleos de eficiencia",
     L"у этого процессора нет энергоэффективных ядер", L"此 CPU 没有能效核心",
     L"この cpu には効率コアがありません", L"이 cpu에는 효율 코어가 없습니다",
     L"لا يحتوي هذا المعالج على أنوية موفرة للطاقة"},

    // about
    {L"About", L"Acerca de", L"О программе", L"关于", L"バージョン情報", L"정보", L"حول"},
    {L"Watches what eats the cpu and lowers its priority, quietly.",
     L"Vigila qué consume la cpu y baja su prioridad, sin ruido.",
     L"Следит, что ест процессор, и тихо понижает приоритет.", L"监视谁在吃 CPU，并悄悄降低它的优先级。",
     L"CPU を食っているものを見張り、そっと優先度を下げます。",
     L"CPU를 먹는 프로세스를 지켜보고 조용히 우선순위를 낮춥니다.",
     L"يراقب ما يستهلك المعالج ويخفض أولويته بهدوء."},
    {L"MIT License - free to use, change and share",
     L"Licencia MIT - libre de usar, modificar y compartir",
     L"Лицензия MIT - свободно использовать, изменять и распространять",
     L"MIT 许可证 - 可自由使用、修改和分发", L"MIT ライセンス - 自由に使用、改変、配布できます",
     L"MIT 라이선스 - 자유롭게 사용, 수정, 배포", L"رخصة MIT - حر في الاستخدام والتعديل والمشاركة"},
    {L"Close", L"Cerrar", L"Закрыть", L"关闭", L"閉じる", L"닫기", L"إغلاق"},
};

int g_lang = kEn;

int from_system() {
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
        case LANG_SPANISH: return kEs;
        case LANG_RUSSIAN: return kRu;
        case LANG_CHINESE: return kZh;
        case LANG_JAPANESE: return kJa;
        case LANG_KOREAN: return kKo;
        case LANG_ARABIC: return kAr;
        default: return kEn;
    }
}

}  // namespace

void set_language(const std::wstring& code) {
    if (code.empty() || code == L"auto") {
        g_lang = from_system();
        return;
    }
    for (int i = 1; kCodes[i]; ++i) {
        if (code == kCodes[i]) {
            g_lang = i - 1;  // kCodes[0] is "auto"
            return;
        }
    }
    g_lang = kEn;
}

const wchar_t* tr(Str id) {
    if (id < 0 || id >= S_COUNT) return L"";
    const wchar_t* s = kText[id][g_lang];
    return s ? s : kText[id][kEn];
}

bool rtl() { return g_lang == kAr; }

const wchar_t* const* language_codes() { return kCodes; }

const wchar_t* language_name(size_t index) {
    return index < sizeof(kNames) / sizeof(kNames[0]) ? kNames[index] : L"";
}

}  // namespace cpulytics
