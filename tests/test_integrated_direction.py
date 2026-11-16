import base64
import unittest
import cv2
from mho_assist.integrated_direction_worker import Recognizer


class IntegratedRecognitionTests(unittest.TestCase):
    def setUp(self):
        self.worker = Recognizer()
        image = cv2.imread('tests/fixtures/frame_36.png')
        crop = cv2.resize(image[377:431, 283:776], (495,54))
        self.rgb = base64.b64encode(cv2.cvtColor(crop,cv2.COLOR_BGR2RGB)).decode()

    def read(self, t, epoch=1):
        return self.worker.handle(dict(epoch=epoch,time=t,rgb=self.rgb))

    def test_stable_sequence_acknowledgement_and_no_repeat(self):
        self.assertFalse(self.read(0)['sequence'])
        sequence = self.read(.08)['sequence']
        self.assertEqual(sequence,['down','left','up'])
        # A dropped or stale UI response must not consume the recognition.
        self.assertEqual(self.read(.12)['sequence'],sequence)
        self.worker.handle(dict(epoch=1,time=.13,commit=sequence))
        for t in [.2,.5,1,2]:
            self.assertFalse(self.read(t)['sequence'])
        self.assertFalse(self.read(2.1,epoch=2)['sequence'])
        self.assertEqual(self.read(2.2,epoch=2)['sequence'],sequence)

    def test_invalid_crop_is_rejected(self):
        with self.assertRaises(ValueError):
            self.worker.handle(dict(epoch=1,time=0,rgb=base64.b64encode(b'x').decode()))

    def test_original_four_button_locator_confirms_two_frames(self):
        image=cv2.imread('tests/fixtures/frame_36.png')
        image=cv2.resize(image,(1000,448))
        request=dict(epoch=1,time=0,locate=True,width=1000,height=448,
                     rgb=base64.b64encode(cv2.cvtColor(image,cv2.COLOR_BGR2RGB)).decode())
        self.assertFalse(self.worker.handle(request)['region'])
        request['time']=.8
        rect=self.worker.handle(request)['region']
        self.assertEqual(len(rect),4)
        for actual,expected in zip(rect,[283/1080,377/484,493/1080,54/484]):
            self.assertAlmostEqual(actual,expected,delta=.009)

    def test_locator_does_not_rearm_a_sent_sequence(self):
        self.read(0);sequence=self.read(.08)['sequence']
        self.worker.handle(dict(epoch=1,time=.1,commit=sequence))
        image=cv2.resize(cv2.imread('tests/fixtures/frame_36.png'),(1000,448))
        self.worker.handle(dict(epoch=1,time=.8,locate=True,width=1000,height=448,
                               rgb=base64.b64encode(cv2.cvtColor(image,cv2.COLOR_BGR2RGB)).decode()))
        self.assertFalse(self.read(1.0)['sequence'])


if __name__ == '__main__':
    unittest.main()
