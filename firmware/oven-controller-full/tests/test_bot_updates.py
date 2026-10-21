import struct
import unittest
import zlib
from collections import deque
from unittest.mock import patch
from urllib.parse import urlsplit, parse_qs
from test_telegram_tool import tool, preview


class BotUpdatesTest(unittest.TestCase):
    def test_status_uses_italian_time_and_summer_winter_offsets(self):
        status = preview.DemoDevice('full', speed=1).status()
        for epoch, expected in ((1792484784, '10:26:24 (Italia)'),
                                (1792890000, '02:00:00 (Italia)')):
            status['utcSec'] = epoch
            text = tool.format_status(status)
            self.assertIn(expected, text)
            self.assertNotIn(' UTC', text)

    def test_start_messages_bypass_essential_and_probe_warnings_are_detailed_only(self):
        status = preview.DemoDevice('full', speed=1).status()
        for mode in tool.MODE_LABELS:
            for title in ('CICLO AVVIATO', 'MANUALE AVVIATO', 'ESP AVVIATO'):
                self.assertIn(title, tool.format_notice(title, status=status, mode=mode))
            notice = tool.format_notice('AVVISO SONDA', status=status, mode=mode)
            self.assertEqual(notice is not None, mode == 'dettagliata')
            self.assertIn('ALLARME SONDA', tool.format_notice('ALLARME SONDA', status=status, critical=True, mode=mode))

    def test_plot_is_a_command_and_has_a_persistent_button(self):
        self.assertIn('/plot', tool.GUIDE_TEXT)
        self.assertEqual(tool.main_keyboard()['keyboard'], [['/notifiche', '/status'], ['/plot15', '/plot30', '/plot60']])
        command = tool.Commands(42)
        message = dict(date=100, text='/plot', chat=dict(id=42, type='private'), **{'from': dict(id=42)})
        self.assertEqual(command.consume([dict(update_id=1, message=message)], 101), ['/plot'])
        message['from']['id'] = 7
        self.assertEqual(command.consume([dict(update_id=2, message=message)], 101), [])

    def test_plot_submenu_buttons_are_authorized_commands(self):
        self.assertEqual(tool.plot_keyboard()['keyboard'][0], list(tool.PLOT_BUTTONS))
        for button, expected in tool.PLOT_BUTTONS.items():
            for sender, date, result in ((42,100,[expected]),(7,100,[]),(42,1,[])):
                commands=tool.Commands(42)
                message=dict(date=date,text=button,chat=dict(id=42,type='private'),**{'from':dict(id=sender)})
                self.assertEqual(commands.consume([dict(update_id=1,message=message)],122),result)

    def test_main_menu_notifications_back_and_legacy_aliases(self):
        for mode in tool.MODE_LABELS:
            with self.subTest(mode=mode):
                main = tool.menu_reply('/menu', mode)
                submenu = tool.menu_reply('/notifiche', mode)
                self.assertEqual(main['reply_markup'], tool.main_keyboard())
                self.assertEqual(submenu['reply_markup'], tool.mode_keyboard())
                self.assertIn(tool.MODE_LABELS[mode], submenu['text'])
                self.assertEqual(tool.mode_keyboard()['keyboard'], [list(tool.MODE_BUTTONS)[:2], [list(tool.MODE_BUTTONS)[2]], [tool.BACK_BUTTON]])
                self.assertEqual(tool.menu_reply('/modalita', mode), submenu)
                self.assertEqual(tool.menu_reply('/help', mode)['reply_markup'], tool.main_keyboard())
                self.assertEqual(tool.plot_keyboard()['keyboard'][-1], [tool.BACK_BUTTON])
                for keyboard in (main['reply_markup'], submenu['reply_markup'], tool.plot_keyboard()):
                    self.assertTrue(keyboard['is_persistent'])
                    self.assertFalse(keyboard['one_time_keyboard'])

    def test_every_visible_button_uses_private_fresh_command_authorization(self):
        buttons = [b for keyboard in (tool.main_keyboard(), tool.mode_keyboard(), tool.plot_keyboard())
                   for row in keyboard['keyboard'] for b in row]
        for button in buttons:
            expected = '/menu' if button == tool.BACK_BUTTON else tool.PLOT_BUTTONS.get(button, tool.MODE_BUTTONS.get(button, button))
            for sender, date, result in ((42, 100, [expected]), (7, 100, []), (42, 1, [])):
                with self.subTest(button=button, sender=sender, date=date):
                    message = dict(date=date, text=button, chat=dict(id=42, type='private'), **{'from': dict(id=sender)})
                    self.assertEqual(tool.Commands(42).consume([dict(update_id=1, message=message)], 122), result)

    def test_plot_windows_read_previous_pages_and_preserve_gaps(self):
        d=preview.DemoDevice('full',speed=1)
        d.now=3600
        d.samples=[[i+1,i*10000,100,102,105,0,11,0] for i in range(361)]
        d.samples[350][6]=0
        d.seq=361
        def local(base,endpoint):
            url=urlsplit(endpoint)
            return d.request('GET',url.path,parse_qs(url.query))
        for minutes in (15,30,60):
            with self.subTest(minutes=minutes), patch.object(tool,'local_json',side_effect=local), patch.object(tool,'plot_png',return_value=b'PNG') as render:
                result=tool.preview_plot('unused',minutes)
                points=render.call_args.args[0]
                self.assertEqual(len(points),minutes*6+1)
                self.assertEqual(points[0]['ms'],(60-minutes)*60000)
                self.assertIsNone(points[-11]['actual'])
                self.assertIn(f'Ultimi {minutes} minuti',result['caption'])
        with self.assertRaises(ValueError):tool.preview_plot('unused',90)

    def test_plot_rejects_reboot_between_history_pages(self):
        with patch.object(tool,'local_json',side_effect=[dict(bootCount=1,uptimeMs=3600000),
                dict(bootCount=1,samples=[[181,1800000,100,100,100,0,11,0]],moreBefore=True),
                dict(bootCount=2,samples=[])]):
            with self.assertRaisesRegex(ValueError,'riavviata'):tool.preview_plot('unused',60)

    def test_png_pixels_crc_and_photo_multipart(self):
        png = tool.plot_png([dict(ms=0,actual=100,target=101),dict(ms=10000,actual=None,target=102),dict(ms=20000,actual=104,target=103)])
        self.assertTrue(png.startswith(b'\x89PNG\r\n\x1a\n'))
        at, data, palette = 8, b'', None
        while at < len(png):
            size = struct.unpack('>I', png[at:at+4])[0]
            kind, body = png[at+4:at+8], png[at+8:at+8+size]
            crc = struct.unpack('>I', png[at+8+size:at+12+size])[0]
            self.assertEqual(crc, zlib.crc32(kind+body)&0xffffffff)
            if kind == b'IDAT': data += body
            if kind == b'PLTE': palette = body
            at += size+12
        raster = zlib.decompress(data)
        self.assertEqual(len(raster), 961*480)
        self.assertTrue(all(raster[y*961]==0 for y in range(480)))
        self.assertIn(4,raster); self.assertIn(5,raster)
        self.assertLess(max(raster),len(palette)//3)
        api = tool.TelegramApi('12345:abcdefghijklmnopqrstuvwxyz_123')
        with patch.object(api, '_request', return_value=dict(message_id=5)) as send:
            api.send_photo(42,'Plot.png',png,caption='Grafico')
        self.assertEqual(send.call_args[0][0],'sendPhoto')
        self.assertIn(b'name="photo"',send.call_args[0][1])
        self.assertIn(b'image/png',send.call_args[0][1])
        self.assertIn(b'\r\ntrue\r\n',send.call_args[0][1])

    def test_program_and_pause_continue_after_twelve_hours(self):
        for paused in (False,True):
            d = preview.DemoDevice('full',speed=1)
            d.temperature = 175
            d.recipes = [dict(id='long',name='Lungo',steps=[dict(type='hold',target=175,duration=1440)])]
            d.request('POST','command',body=dict(action='start',recipeId='long',hardwareReady=True))
            if paused: d.request('POST','command',body=dict(action='pause'))
            d.now += 13*3600
            d.advance_full(0)
            self.assertEqual(d.phase,'paused' if paused else 'running')
            self.assertFalse(d.fault)


if __name__ == '__main__':
    unittest.main()
