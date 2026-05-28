# AI - 基于预训练 MobileNetV2 的实时图像分类

## 硬件
- 树莓派 4B/5B x1
- Pi Camera 官方摄像头 x1

## 原理
使用 TensorFlow 官方预训练的 MobileNetV2 量化模型，可识别 ImageNet 1000 类常见物体，**无需任何训练**。模型已 TFLite INT8 量化，在树莓派上推理约 100~200ms。

## 安装
```bash
# 树莓派上执行
sudo apt install python3-pip python3-picamera2
pip3 install -r requirements.txt
```

如果 tflite-runtime 安装失败，可以改用 TensorFlow Lite:
```bash
pip3 install tensorflow  # 完整版，约200MB
# 然后在 infer.py 中会自动 fallback
```

## 运行
```bash
python3 infer.py
```
首次运行会自动下载 MobileNetV2 模型 (~13MB) 和 ImageNet 标签文件。
然后浏览器访问 http://<树莓派IP>:5000

## 演示流程
1. Flask 网页投屏: 左边摄像头实时画面, 右边 Top-5 分类结果
2. 摄像头对准水杯: coffee mug 92%
3. 摄像头对准书本: book jacket 87%
4. 摄像头对准手机: cell phone 95%
5. 摄像头对准人: person 89%
6. 展示推理速度: 约100-200ms/帧

## 报告素材
- 模型选型分析: 为什么选 MobileNetV2 (轻量/量化/边缘)
- 量化对比: FP32 vs INT8 模型大小和速度
- 推理速度实测: 10次平均耗时
- 迁移学习思路: 如需定制类别, 如何Fine-tune
- 1000类中选出5-10个常见物体, 做准确率测试

## 文件说明
- infer.py: 主程序 (拍照+推理+Flask)
- requirements.txt: Python 依赖
- 模型和标签首次运行自动下载
