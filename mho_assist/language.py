# -*- coding: utf-8 -*-
"""All visible labels, status explanations and chart tooltips in two languages."""
import re
TEXT={
'title':('方向序列助手','Arrow Assistant'), 'hotkeys':('F8 启用/暂停 · F9 急停 · F10 回游戏','F8 toggle · F9 pause · F10 game'),
'choose':('选择目标窗口，或框选范围','Select a window, or select a screen area'), 'refresh':('刷新','Refresh'),
'select':('框选','Area'), 'start':('启动预览','Preview'), 'window':('窗口模式','Window'), 'relocate':('重新定位','Relocate'),
'pause':('暂停 F9','Pause F9'), 'settings':('设置','Settings'), 'tools':('投屏工具','Game tools'),
'advanced':('识别与输入参数','Recognition and input'), 'hold':('按住','Hold'), 'gap':('间隔','Gap'), 'threshold':('阈值','Threshold'),
'hold_jitter':('按住抖动 ±','Hold jitter ±'), 'gap_jitter':('间隔抖动 ±','Gap jitter ±'), 'strict':('未确认则暂停','Pause if unconfirmed'),
'hint':('参数修改后重新启动预览生效','Restart preview to apply parameters'), 'preview':('箭头序列预览','Arrow sequence preview'),
'session':('当次启动','This session'), 'history':('全部历史','All history'), 'rounds':('识别轮次','Recognized rounds'),
'keys':('方向键总数','Recognized keys'), 'confidence':('平均置信度','Mean confidence'), 'speed':('平均识别耗时','Mean detection time'),
'directions':('方向分布','Direction distribution'), 'recent':('最近轮次 · 按键构成','Recent rounds · key mix'),
'conf_hist':('轮次置信度 · %','Round confidence · %'), 'latency':('识别耗时折线 · ms','Detection time · ms'),
'empty':('等待统计数据','Waiting for data'), 'scope_hint':('识别含预览；确认来自变黄反馈；耗时仅含图像算法','Recognition includes preview; confirmed = yellow feedback; time = algorithm only'),
'history_start':('历史从 V4 开始累计','History starts with V4'), 'sent':('已发送','Sent'), 'confirmed':('已确认','Confirmed'),
'logs':('历史 / 日志','History / logs'), 'idle':('先选择窗口，再启动预览','Choose a window, then start preview'),
'enabled':('自动输入','Auto input'), 'paused':('预览 / 暂停','Preview / paused'),
'qt_open':('打开 / 连接 QtScrcpy','Open / connect QtScrcpy'), 'game_focus':('切回游戏 F10','Focus game F10'),
'qt_configure':('配置 QtScrcpy 路径','Set QtScrcpy path'), 'qt_configured':('QtScrcpy 路径已保存','QtScrcpy path saved'),
'qt_invalid':('请选择 QtScrcpy.exe','Choose QtScrcpy.exe'),
'qt_missing':('无法启动 QtScrcpy。请在投屏工具菜单配置 QtScrcpy 路径，或手动打开投屏后选择窗口。','Could not start QtScrcpy. Set its path in Game tools, or open it manually and select the mirror window.'),
'recover':('恢复游戏控制','Recover game control'), 'editor':('编辑按键位置','Edit key positions'), 'touches':('清理手机残留触点','Clear stale phone touches'),
'folder':('游戏工具文件夹','Game tools folder'), 'mapping':('映射配置参考：{key} · 助手：F8','Mapping key hint: {key} · assistant: F8'),
'binding':('已关联投屏窗口并启动预览；切回投屏后按 F8','Mirror linked and preview started; focus the mirror and press F8'),
'waiting_phone':('等待投屏窗口；请在 QtScrcpy 连接手机','Waiting for mirror; connect your phone in QtScrcpy'),
'multiple':('检测到多个投屏窗口，请在列表选择','Multiple mirrors found; select one from the list'),
'tool_missing':('工具入口不存在：{path}','Tool shortcut not found: {path}'), 'tool_error':('无法打开工具：{path}','Cannot open tool: {path}'),
'speed_tip':('每点为一段时间内图像识别的平均耗时，不包含投屏、按键和手机响应','Each point averages detector calls in one time slice; excludes mirroring, key delivery and phone response'),
'confidence_tip':('每个稳定轮次的图案匹配分数，不是按键成功率','Template match scores per stable round; not a key-delivery success rate'),
'repeat':('连续刷','Auto repeat'), 'repeat_tip':('开启后，在已启用输入且目标位于前台时，确认结束页面后按 Shift → [ → ] → ] 重开，各步至少间隔一秒；F9 立即暂停','When armed and focused, verify the ending page then tap Shift → [ → ] → ] with at least 1 s between steps. F9 pauses immediately'),
'cycle_playing':('三轮步骤按每轮约 10 组序列判断','Phases inferred from approximately 10 groups each'),
'cycle_navigation':('正在重开：{key}','Restarting: {key}'), 'cycle_change_wait':('核验页面变化：{key}','Checking page change: {key}'),
'cycle_next_wait':('等待下一局开始','Waiting for next game'), 'cycle_error':('连续刷已暂停：页面变化未确认；请检查映射，关闭再开启连续刷','Auto repeat paused: page change not confirmed. Check mapping, then toggle repeat off/on'),
'cycle_paused':('重开流程已暂停','Restart sequence paused'), 'cycle_games':('已完成 {n} 局','Completed {n}'),
}
def tr(identifier,lang='zh',**values):return TEXT.get(identifier,(identifier,identifier))[lang=='en'].format(**values)

REASONS={
'等待目标窗口进入前台':'Waiting for target window', '目标不在前台，不发送按键':'Target is not in the foreground; no keys sent',
'目标窗口太小':'Target window is too small', '正在自动定位箭头区域':'Locating arrow region',
'定位动画中，等待区域稳定':'Waiting for region animation to settle', '正在寻找四方向按钮':'Looking for four direction buttons',
'等待完整序列':'Waiting for a complete sequence', '未识别到完整、颜色有效且达到阈值的序列':'Waiting for a complete, valid sequence above threshold',
'本轮已发送，等待新序列（防止重复输入）':'Round already sent; waiting for a new round', '序列已稳定，可以输入':'Sequence is stable and ready',
'箭头弹出中，等待位置稳定':'Arrows are moving; waiting for stable positions',
'自动输入未启用，按 F8；预览不会消耗本轮':'Input is paused; press F8. Preview does not consume this round',
'发送中断，按 F8 恢复':'Input interrupted; press F8 to resume', '已发送本轮，等待黄色反馈':'Round sent; waiting for yellow feedback',
'请先启动预览':'Start preview first', '请先切回目标窗口，再按 F8':'Focus the target window, then press F8',
'请选择目标窗口或重新框选范围':'Choose a target window or select an area', '不能监测助手自己的窗口，请重新框选游戏':'Select the game, not the assistant window',
'请先选择目标窗口':'Choose a target window first', '框选中心未找到有效目标窗口，请重新框选':'No target at the selection center; select again',
}
def reason(text,lang):
    if lang!='en':return text
    if text in REASONS:return REASONS[text]
    if text.startswith('等待黄色反馈 '):return 'Waiting for yellow feedback '+text.split(' ')[-1]
    if text.startswith('等待连续帧确认 '):return 'Confirming consecutive frames '+text.split(' ')[-1]
    if text.startswith('停止：'):return 'Stopped; see session log'
    return 'See session log for details'
