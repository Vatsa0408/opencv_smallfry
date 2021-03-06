# -*- coding: utf-8 -*-
"""text.crnn.ipynb

Automatically generated by Colaboratory.

Original file is located at
    https://colab.research.google.com/drive/1v1WnpseX1zX5OgEJshxbP4-dvmhbSRIb
"""

!pip install opencv-python==4.2.0.32

# Commented out IPython magic to ensure Python compatibility.
!git clone https://github.com/meijieru/crnn.pytorch
# %cd crnn.pytorch
!wget "https://www.dropbox.com/s/dboqjk20qjkpta3/crnn.pth?dl=0" -O crnn.pth

# Commented out IPython magic to ensure Python compatibility.
# %cd /content/crnn.pytorch

import torch
import models.crnn as crnn

model = crnn.CRNN(32, 1, 37, 256)
model.load_state_dict(torch.load("crnn.pth"))
model.eval()

# Generate a dummy input that is consistent with the network's architecture
dummy_input = torch.randn(1, 1, 37, 256)
 
# Export into an ONNX model using the PyTorch model and the dummy input
torch.onnx.export(model, dummy_input, "crnn.onnx", verbose=True)

import cv2
class ConstantOfShapeLayer(object):
    def __init__(self, params, blobs):
        self.value=blobs[0]
        self.shape=blobs[1]
    def getMemoryShapes(self, inputs):
        return [self.shape]
    def forward(self, inputs):
        return [np.ones(self.shape) * self.value]

#cv2.dnn_registerLayer('ConstantOfShape', ConstantOfShapeLayer)
net=cv2.dnn.readNet("crnn.onnx")

!pip install onnx

!python /usr/local/lib/python3.6/dist-packages/onnx/tools/net_drawer.py --input=crnn.onnx --output=onnx.dot

!dot onnx.dot -Tpng -oonnx_dot.png

import cv2
im = cv2.imread("onnx_dot.png")
print(im.shape)

#from google.colab.patches import cv2_imshow
#cv2_imshow(im)