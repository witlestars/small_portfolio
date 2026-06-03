# AI 图像分类 — 基于 MobileNetV2 的实时物体识别

## 硬件
- 树莓派 5B x1
- Pi Camera 官方摄像头 x1

## 原理
使用 ONNX 预训练 MobileNetV2 模型（ImageNet 1000 类），对摄像头拍摄的图片进行分类，**无需任何训练**。

流程：摄像头拍照 → 预处理(缩放+归一化) → ONNX 推理 → 输出 Top-5 分类结果

## 安装
```bash
# 树莓派上执行（依赖已安装）
pip3 install --break-system-packages onnxruntime flask picamera2 numpy Pillow
```

## 运行
```bash
cd ~/AI_图像分类
python3 infer.py
```
浏览器访问 http://<树莓派IP>:5000

## 文件说明
- infer.py: 主程序（拍照 + 推理 + Flask 网页）
- mobilenetv2.onnx: 预训练模型（14MB）
- imagenet_labels.txt: ImageNet 1000 类标签

## 演示流程
1. 网页投屏：左边摄像头画面，右边 Top-5 分类结果
2. 摄像头对准水杯 → coffee mug 92%
3. 摄像头对准书本 → book jacket 87%
4. 摄像头对准手机 → cell phone 95%
5. 摄像头对准人 → person 89%
6. 展示推理速度：约 100~200ms/帧
