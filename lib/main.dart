import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

const _channel = MethodChannel('akl/bridge');

void main() => runApp(const AkLoaderApp());

class AkLoaderApp extends StatelessWidget {
  const AkLoaderApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: '明日方舟启动器',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorSchemeSeed: const Color(0xFF2E5BE0),
        useMaterial3: true,
        brightness: Brightness.dark,
      ),
      home: const HomePage(),
    );
  }
}

class GameEntry {
  GameEntry({
    required this.pkg,
    required this.label,
    required this.short,
    required this.installed,
    this.version,
  });

  final String pkg;
  final String label;
  final String short;
  final bool installed;
  final String? version;

  factory GameEntry.fromMap(Map<dynamic, dynamic> m) => GameEntry(
        pkg: m['pkg'] as String,
        label: m['label'] as String,
        short: m['short'] as String,
        installed: m['installed'] as bool,
        version: m['version'] as String?,
      );
}

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  bool _loading = true;
  bool _enabled = true;
  double _delaySec = 5;
  double _speed = 2.0;
  bool _cost = false;
  bool _life = false;
  bool _freeze = false;
  bool _win = false;
  List<GameEntry> _games = const [];

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    try {
      final cfg = await _channel.invokeMapMethod<String, dynamic>('config');
      final raw = await _channel.invokeMethod<List<dynamic>>('games');
      if (!mounted) return;
      setState(() {
        _enabled = (cfg?['enabled'] as bool?) ?? true;
        _delaySec = (((cfg?['delayMs'] as int?) ?? 5000) / 1000).toDouble();
        _speed = (((cfg?['speedX100'] as int?) ?? 200) / 100).toDouble();
        _cost = (cfg?['cost'] as bool?) ?? false;
        _life = (cfg?['life'] as bool?) ?? false;
        _freeze = (cfg?['freeze'] as bool?) ?? false;
        _win = (cfg?['win'] as bool?) ?? false;
        _games = (raw ?? const [])
            .map((e) => GameEntry.fromMap(e as Map))
            .toList(growable: false);
        _loading = false;
      });
    } catch (e) {
      if (!mounted) return;
      setState(() => _loading = false);
      _toast('读取配置失败：$e');
    }
  }

  Future<void> _save() async {
    await _channel.invokeMethod<void>('saveConfig', {
      'enabled': _enabled,
      'delayMs': (_delaySec * 1000).round(),
      'speedX100': (_speed * 100).round(),
      'cost': _cost,
      'life': _life,
      'freeze': _freeze,
      'win': _win,
    });
  }

  Future<void> _launch(GameEntry g) async {
    if (!g.installed) {
      _toast('${g.label} 未安装');
      return;
    }
    await _save();
    final ok = await _channel.invokeMethod<bool>('launch', {'pkg': g.pkg}) ?? false;
    if (!mounted) return;
    _toast(ok ? '正在启动${g.short}…' : '启动失败');
  }

  void _toast(String msg) {
    ScaffoldMessenger.of(context)
      ..clearSnackBars()
      ..showSnackBar(SnackBar(content: Text(msg), duration: const Duration(seconds: 2)));
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('明日方舟 启动器')),
      body: _loading
          ? const Center(child: CircularProgressIndicator())
          : ListView(
              padding: const EdgeInsets.all(16),
              children: [
                _engineCard(),
                const SizedBox(height: 16),
                const Text('点击图标启动；参数在启动游戏的瞬间生效。',
                    style: TextStyle(color: Colors.white60, fontSize: 12)),
                const SizedBox(height: 12),
                ..._games.map(_gameTile),
              ],
            ),
    );
  }

  Widget _engineCard() {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                const Icon(Icons.memory),
                const SizedBox(width: 8),
                const Text('载入引擎', style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600)),
                const Spacer(),
                Switch(
                  value: _enabled,
                  onChanged: (v) {
                    setState(() => _enabled = v);
                    _save();
                  },
                ),
              ],
            ),
            const Divider(height: 24),
            _sliderRow(
              label: '延迟注入',
              valueText: '${_delaySec.round()} s',
              value: _delaySec,
              min: 0,
              max: 30,
              divisions: 30,
              onChanged: _enabled
                  ? (v) => setState(() => _delaySec = v)
                  : null,
              onChangeEnd: (_) => _save(),
            ),
            _sliderRow(
              label: '倍速',
              valueText: 'x${_speed.toStringAsFixed(1)}',
              value: _speed,
              min: 1,
              max: 10,
              divisions: 18,
              onChanged: _enabled
                  ? (v) => setState(() => _speed = v)
                  : null,
              onChangeEnd: (_) => _save(),
            ),
            const Divider(height: 24),
            const Align(
              alignment: Alignment.centerLeft,
              child: Text('战斗功能', style: TextStyle(fontWeight: FontWeight.w600)),
            ),
            _switchRow('无限部署费用', 'BattleController.SetCost 覆写为 9999',
                _cost, (v) { setState(() => _cost = v); _save(); }),
            _switchRow('生命不减', 'ModifyLifePoint 的扣血值钳到 0',
                _life, (v) { setState(() => _life = v); _save(); }),
            _switchRow('敌人停滞', 'BattleEnemy.MoveByRoute 速度置 0',
                _freeze, (v) { setState(() => _freeze = v); _save(); }),
            _switchRow('强制胜利', 'get_gameResult 返回三星结果',
                _win, (v) { setState(() => _win = v); _save(); }),
            const SizedBox(height: 8),
            const Text('功能在战斗开始时按名称解析后挂钩；未进入战斗前无影响。',
                style: TextStyle(color: Colors.white38, fontSize: 11)),
          ],
        ),
      ),
    );
  }

  Widget _switchRow(String title, String subtitle, bool value, ValueChanged<bool> onChanged) {
    return SwitchListTile(
      dense: true,
      contentPadding: EdgeInsets.zero,
      title: Text(title, style: const TextStyle(fontSize: 14)),
      subtitle: Text(subtitle, style: const TextStyle(color: Colors.white38, fontSize: 11)),
      value: value,
      onChanged: _enabled ? onChanged : null,
    );
  }

  Widget _sliderRow({
    required String label,
    required String valueText,
    required double value,
    required double min,
    required double max,
    required int divisions,
    required ValueChanged<double>? onChanged,
    required ValueChanged<double>? onChangeEnd,
  }) {
    return Column(
      children: [
        Row(
          children: [
            Text(label, style: const TextStyle(color: Colors.white70)),
            const Spacer(),
            Text(valueText, style: const TextStyle(fontWeight: FontWeight.w600)),
          ],
        ),
        Slider(
          value: value,
          min: min,
          max: max,
          divisions: divisions,
          label: valueText,
          onChanged: onChanged,
          onChangeEnd: onChangeEnd,
        ),
      ],
    );
  }

  Widget _gameTile(GameEntry g) {
    return Card(
      margin: const EdgeInsets.only(bottom: 12),
      child: ListTile(
        leading: CircleAvatar(
          backgroundColor:
              g.installed ? const Color(0xFF2E5BE0) : Colors.white24,
          child: Text(g.short,
              style: const TextStyle(fontSize: 12, color: Colors.white)),
        ),
        title: Text(g.label),
        subtitle: Text(
          g.installed ? '已安装 · v${g.version ?? '?'}' : '未安装',
          style: TextStyle(color: g.installed ? Colors.white54 : Colors.redAccent),
        ),
        trailing: FilledButton.icon(
          onPressed: g.installed ? () => _launch(g) : null,
          icon: const Icon(Icons.play_arrow),
          label: const Text('启动'),
        ),
      ),
    );
  }
}
