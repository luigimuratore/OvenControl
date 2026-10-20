import copy
import unittest
from test_telegram_tool import preview, tool


class FaultResumePreviewTest(unittest.TestCase):
    def command(self, d, action):
        return d.request('POST','command',body=dict(action=action))

    def start(self, kind='hold'):
        d=preview.DemoDevice('full',speed=1)
        d.temperature=100 if kind=='ramp' else 175
        d.recipes=[dict(id='resume',name='Da recuperare',steps=[
            dict(type=kind,target=175,rate=2,duration=3),dict(type='cooldown',target=25,rate=1)])]
        d.request('POST','command',body=dict(action='start',recipeId='resume',hardwareReady=True))
        return d

    def fault(self,d):
        d.request('POST','preview-sensor',body=dict(missing=True))
        d.request('POST','preview-sensor',body=dict(missing=False))
        self.assertEqual(d.phase,'fault')
        self.assertFalse(d.relay)

    def test_ramp_resumes_same_reference_step_and_rate_after_wait(self):
        d=self.start('ramp');d.advance(30)
        target,index,elapsed=d.target,d.step_index,d.step_elapsed
        self.fault(d);alarm=copy.deepcopy(d.last_report)
        d.advance(120)
        self.assertTrue(d.status()['faultResume']['ready'])
        self.command(d,'resetContinue')
        self.assertEqual((d.target,d.step_index,d.step_elapsed),(target,index,elapsed))
        self.assertEqual(d.active['steps'][index]['rate'],2)
        self.assertEqual(d.report_work['pausedMs'],120000)
        self.assertEqual(d.report_work['pauseCount'],1)
        self.assertEqual(d.last_report,alarm)
        d.advance(1);self.assertAlmostEqual(d.target,target+2/60)

    def test_hold_counts_only_fresh_intervals_and_eventual_report_completes(self):
        d=self.start();d.advance(10)
        held,active=d.hold,d.report_work['activeMs']
        self.fault(d);alarm_key=d.last_report['key'];d.advance(60)
        d.temperature=175
        self.command(d,'resetContinue')
        self.assertEqual(d.hold,held)
        self.assertEqual(d.report_work['activeMs'],active)
        d.advance(1);self.assertEqual(d.hold,held)
        d.advance(1);self.assertEqual(d.hold,held+1)
        # Finish the remaining hold, then reach the final cooldown endpoint.
        d.hold=179;d.temperature=175;d.advance(1)
        self.assertEqual(d.step_index,1)
        d.target=25;d.temperature=25;d.advance(1)
        self.assertEqual(d.phase,'complete')
        self.assertEqual(d.last_report['outcome'],'completed')
        self.assertEqual(d.last_report['completedSteps'],2)
        self.assertNotEqual(d.last_report['key'],alarm_key)
        self.assertEqual(d.last_report['pausedMs'],60000)

    def test_reset_block_stop_and_emergency_discard_continuation(self):
        for action in ('reset','stop','emergency'):
            with self.subTest(action=action):
                d=self.start();self.fault(d)
                self.command(d,action)
                self.assertFalse(d.status()['faultResume']['ready'])
                with self.assertRaises(preview.ApiError):self.command(d,'resetContinue')
                self.assertFalse(d.relay)
        d=preview.DemoDevice('full',speed=1)
        d.phase='interrupted'
        with self.assertRaises(preview.ApiError):self.command(d,'resetContinue')

    def test_failed_checks_leave_fault_and_checkpoint_in_place(self):
        for condition in ('missing','relative','absolute'):
            with self.subTest(condition=condition):
                d=self.start();self.fault(d)
                if condition=='missing':d.missing=True
                else:d.temperature=190 if condition=='relative' else 415
                with self.assertRaises(preview.ApiError):self.command(d,'resetContinue')
                self.assertEqual(d.phase,'fault');self.assertIsNotNone(d.fault_resume)
                self.assertEqual(d.duty,0);self.assertFalse(d.relay)
                d.missing=False;d.temperature=175
                self.command(d,'resetContinue');self.assertEqual(d.phase,'running')

    def test_manual_retains_target_rate_and_active_duration(self):
        d=preview.DemoDevice('full',speed=1)
        d.request('POST','manual',body=dict(action='start',targetC=175,rateCMin=2,hardwareReady=True))
        d.advance(10);self.fault(d)
        reference=d.fault_resume['values']['target'];d.advance(60)
        self.command(d,'resetContinue')
        s=d.status()
        self.assertEqual((s['manual']['targetC'],s['manual']['rateCMin']),(175,2))
        self.assertEqual(s['manual']['elapsedSec'],10)
        self.assertEqual(d.target,reference)
        self.assertIsNone(d.last_report)
        d.advance(1);self.assertEqual(d.status()['manual']['elapsedSec'],11)

    def test_paused_program_resumes_and_changed_settings_invalidate_checkpoint(self):
        d=self.start();d.advance(5);self.command(d,'pause');self.fault(d);d.advance(10)
        d.temperature=175
        self.command(d,'resetContinue')
        self.assertEqual(d.phase,'running')
        self.assertEqual(d.target,175)
        self.assertEqual(d.report_work['pauseCount'],1)
        self.fault(d)
        d.request('POST','pid',body=dict(kp=12,ki=.02,kd=50))
        self.assertFalse(d.status()['faultResume']['ready'])

    def test_preview_bot_reports_explicit_continuation(self):
        events=tool.PreviewEvents(dict(events=[]),dict(phase='fault'))
        notices=events.consume(dict(events=[dict(seq=1,level='CICLO',message='Comando simulato: resetContinue')]),dict(phase='running'))
        self.assertEqual(notices[0][0],'CICLO RIPRESO')


if __name__=='__main__':unittest.main()
