import unittest
from unittest.mock import patch
from test_telegram_tool import preview


class SensorAvailabilityTest(unittest.TestCase):
    def test_idle_also_reports_both_missing_without_auto_start(self):
        d=preview.DemoDevice('full',speed=1)
        d.request('POST','preview-sensor',body=dict(missing=True,both=True))
        start=d.now
        self.assertEqual(d.phase,'idle');self.assertFalse(d.status()['ready'])
        d.now=start+20;d.advance_full(0)
        self.assertEqual(d.phase,'fault');self.assertFalse(d.relay)
        original=d.fault
        d.now+=100;d.advance_full(0)
        self.assertEqual(d.fault,original)
    def start(self, mode):
        d=preview.DemoDevice('full',speed=1)
        if mode=='manual': d.request('POST','manual',body=dict(action='start',targetC=50,rateCMin=1,hardwareReady=True))
        elif mode=='test': d.request('POST','test/command',body=dict(action='relayPulse',durationMs=600000))
        else:
            d.request('POST','command',body=dict(action='start',recipeId=d.recipes[0]['id'],hardwareReady=True))
            if mode=='pause':d.request('POST','command',body=dict(action='pause'))
        return d

    def test_either_single_probe_can_fail_indefinitely_without_stopping(self):
        for mode in ('program','manual','pause','test'):
            for index in (0,1):
                with self.subTest(mode=mode,index=index):
                    d=self.start(mode)
                    if index==0:d.missing_first=True
                    else:d.request('POST','preview-sensor',body=dict(missing=True))
                    initial=d.phase
                    d.now+=120;d.advance_full(0)
                    self.assertEqual(d.phase,initial)
                    self.assertFalse(d.status()['sensorRecovery']['waiting'])
                    self.assertEqual(d.status()['sensorRecovery']['validProbes'],1)
                    self.assertEqual(len(d.available_temperatures()),1)

    def test_both_missing_switch_off_immediately_and_alarm_at_exact_timeout(self):
        for mode in ('program','manual','pause','test'):
            for timeout in (1,20,300):
                with self.subTest(mode=mode,timeout=timeout):
                    d=preview.DemoDevice('full',speed=1)
                    d.request('POST','sensor-recovery',body=dict(bothFaultTimeoutSec=timeout))
                    if mode=='manual':d.request('POST','manual',body=dict(action='start',targetC=50,rateCMin=1,hardwareReady=True))
                    elif mode=='test':d.request('POST','test/command',body=dict(action='relayPulse',durationMs=600000))
                    else:
                        d.request('POST','command',body=dict(action='start',recipeId=d.recipes[0]['id'],hardwareReady=True))
                        if mode=='pause':d.request('POST','command',body=dict(action='pause'))
                    initial=d.phase
                    d.request('POST','preview-sensor',body=dict(missing=True,both=True))
                    start=d.now
                    self.assertFalse(d.status()['relay']);self.assertEqual(d.duty,0)
                    self.assertEqual(d.phase,initial)
                    d.now=start+timeout-.001;d.advance_full(0)
                    self.assertEqual(d.phase,initial)
                    d.now=start+timeout;d.advance_full(0)
                    self.assertEqual(d.phase,'fault');self.assertFalse(d.relay)
                    self.assertFalse(d.test_enabled)
                    self.assertIn(str(timeout),d.fault)
                    d.request('POST','preview-sensor',body=dict(missing=False,both=True))
                    self.assertEqual(d.phase,'fault') # A latched alarm never auto-starts.

    def test_recovery_of_one_probe_resets_continuous_absence_interval(self):
        d=self.start('program')
        d.request('POST','preview-sensor',body=dict(missing=True,both=True));start=d.now
        d.now=start+19;d.advance_full(0)
        d.request('POST','preview-sensor',body=dict(missing=False)) # Only PT100 #2 recovers.
        self.assertEqual(d.status()['sensorRecovery']['validProbes'],1)
        self.assertFalse(d.status()['sensorRecovery']['waiting'])
        d.request('POST','preview-sensor',body=dict(missing=True))
        d.now=start+20;d.advance_full(0)
        self.assertEqual(d.phase,'running')
        d.now=start+39;d.advance_full(0)
        self.assertEqual(d.phase,'fault')

    def test_only_available_temperature_drives_limits_and_statistics(self):
        for good in (0,1):
            d=self.start('manual');d.target=50
            values=[dict(c=999,valid=False),dict(c=999,valid=False)];values[good]=dict(c=49,valid=True)
            with patch.object(d,'sensors',return_value=values):
                d.advance_full(0)
                self.assertEqual(d.phase,'running')
                self.assertGreater(d.duty,0)
                self.assertGreater(d.report_work['stats']['trackingSamples'],0)
                self.assertLess(d.report_work['stats']['maxOvershootC'],1)
                self.assertFalse(d.check_probe_loss())
            values[good]=dict(c=415,valid=True)
            with patch.object(d,'sensors',return_value=values):
                d.advance_full(0)
                self.assertEqual(d.phase,'fault');self.assertFalse(d.relay)


if __name__=='__main__':unittest.main()
