# -*- coding: utf-8 -*-
"""WM811K 웨이퍼 불량 분류 모델을 학습하고 실제 ONNX 모델로 내보냅니다.

Google Colab에서 실행하는 용도로 작성했습니다.

생성되는 파일:
- AI_Model/wafer_defect_model.pth
- AI_Model/wafer_defect_model.onnx
- AI_Model/wafer_labels.json
- Assets/TestImages/normal_wafer.png
- Assets/TestImages/scratch_wafer.png

Kaggle API 정보는 보안을 위해 이 파일에 저장하지 않습니다.
아래 두 방법 중 하나를 사용하세요.
1. Colab의 /content/kaggle.json 위치에 kaggle.json 업로드
2. Colab 실행 환경에서 KAGGLE_USERNAME, KAGGLE_KEY 환경변수 설정
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import zipfile
from pathlib import Path

import cv2
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import torch
from torch import nn
from torch.utils.data import DataLoader, Dataset


IMAGE_SIZE = 64
LABELS = [
    "none",
    "Center",
    "Donut",
    "Edge-Loc",
    "Edge-Ring",
    "Loc",
    "Near-full",
    "Random",
    "Scratch",
]


class WaferDataset(Dataset):
    def __init__(self, frame: pd.DataFrame, label_to_index: dict[str, int]):
        self.frame = frame.reset_index(drop=True)
        self.label_to_index = label_to_index

    def __len__(self) -> int:
        return len(self.frame)

    def __getitem__(self, index: int) -> tuple[torch.Tensor, torch.Tensor]:
        row = self.frame.iloc[index]
        image = preprocess_wafer(row["waferMap"])
        label = self.label_to_index[row["failureType"]]
        return torch.from_numpy(image), torch.tensor(label, dtype=torch.long)


class WaferCNN(nn.Module):
    def __init__(self, num_classes: int):
        super().__init__()
        self.features = nn.Sequential(
            nn.Conv2d(1, 16, kernel_size=3, padding=1),
            nn.BatchNorm2d(16),
            nn.ReLU(),
            nn.MaxPool2d(2),
            nn.Conv2d(16, 32, kernel_size=3, padding=1),
            nn.BatchNorm2d(32),
            nn.ReLU(),
            nn.MaxPool2d(2),
            nn.Conv2d(32, 64, kernel_size=3, padding=1),
            nn.BatchNorm2d(64),
            nn.ReLU(),
            nn.MaxPool2d(2),
        )
        self.classifier = nn.Sequential(
            nn.Flatten(),
            nn.Linear(64 * 8 * 8, 128),
            nn.ReLU(),
            nn.Dropout(0.25),
            nn.Linear(128, num_classes),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        x = self.features(x)
        return self.classifier(x)


def run(command: list[str]) -> None:
    print("실행 중:", " ".join(command))
    subprocess.run(command, check=True)


def setup_kaggle_credentials() -> None:
    if os.environ.get("KAGGLE_USERNAME") and os.environ.get("KAGGLE_KEY"):
        return

    source = Path("/content/kaggle.json")
    if not source.exists():
        raise RuntimeError(
            "Kaggle 인증 정보를 찾을 수 없습니다. /content/kaggle.json 파일을 업로드하거나 "
            "KAGGLE_USERNAME, KAGGLE_KEY 환경변수를 설정하세요."
        )

    kaggle_dir = Path.home() / ".kaggle"
    kaggle_dir.mkdir(exist_ok=True)
    target = kaggle_dir / "kaggle.json"
    shutil.copy2(source, target)
    target.chmod(0o600)


def ensure_dataset(dataset_dir: Path) -> Path:
    dataset_dir.mkdir(parents=True, exist_ok=True)
    pkl_path = dataset_dir / "LSWMD.pkl"
    if pkl_path.exists():
        print(f"데이터셋이 이미 존재합니다: {pkl_path}")
        return pkl_path

    setup_kaggle_credentials()
    run(
        [
            "kaggle",
            "datasets",
            "download",
            "-d",
            "qingyi/wm811k-wafer-map",
            "-p",
            str(dataset_dir),
        ]
    )

    zip_path = dataset_dir / "wm811k-wafer-map.zip"
    with zipfile.ZipFile(zip_path, "r") as archive:
        archive.extractall(dataset_dir)

    if not pkl_path.exists():
        raise FileNotFoundError(f"{dataset_dir} 안에서 LSWMD.pkl 파일을 찾지 못했습니다.")
    return pkl_path


def clean_labels(series: pd.Series) -> pd.Series:
    return series.astype(str).str.replace(r"\[|\]|'", "", regex=True).str.strip()


def load_balanced_frame(pkl_path: Path, max_per_class: int) -> pd.DataFrame:
    df = pd.read_pickle(pkl_path)
    df["failureType"] = clean_labels(df["failureType"])
    df = df[df["failureType"].isin(LABELS)].copy()
    df = df[df["waferMap"].notna()]

    parts = []
    for label in LABELS:
        group = df[df["failureType"] == label]
        if group.empty:
            print(f"주의: 해당 라벨을 찾지 못했습니다: {label}")
            continue
        sample_count = min(max_per_class, len(group))
        parts.append(group.sample(n=sample_count, random_state=42))

    result = pd.concat(parts).sample(frac=1.0, random_state=42).reset_index(drop=True)
    print("학습에 사용할 클래스별 샘플 수:")
    print(result["failureType"].value_counts())
    return result


def preprocess_wafer(wafer_map: object) -> np.ndarray:
    image = np.asarray(wafer_map, dtype=np.float32)
    image = cv2.resize(image, (IMAGE_SIZE, IMAGE_SIZE), interpolation=cv2.INTER_NEAREST)
    image = image / 2.0
    return image[np.newaxis, :, :].astype(np.float32)


def split_train_val(frame: pd.DataFrame, val_ratio: float) -> tuple[pd.DataFrame, pd.DataFrame]:
    train_parts = []
    val_parts = []

    for _, group in frame.groupby("failureType"):
        group = group.sample(frac=1.0, random_state=42)
        val_count = max(1, int(len(group) * val_ratio))
        val_parts.append(group.iloc[:val_count])
        train_parts.append(group.iloc[val_count:])

    train_df = pd.concat(train_parts).sample(frac=1.0, random_state=42).reset_index(drop=True)
    val_df = pd.concat(val_parts).sample(frac=1.0, random_state=42).reset_index(drop=True)
    return train_df, val_df


@torch.no_grad()
def accuracy(model: nn.Module, loader: DataLoader, device: torch.device) -> float:
    model.eval()
    correct = 0
    total = 0
    for images, targets in loader:
        images = images.to(device)
        targets = targets.to(device)
        outputs = model(images)
        predictions = outputs.argmax(dim=1)
        correct += (predictions == targets).sum().item()
        total += targets.numel()
    return correct / max(total, 1)


def train(frame: pd.DataFrame, epochs: int, batch_size: int, learning_rate: float) -> WaferCNN:
    train_df, val_df = split_train_val(frame, val_ratio=0.2)
    label_to_index = {label: index for index, label in enumerate(LABELS)}

    train_loader = DataLoader(
        WaferDataset(train_df, label_to_index),
        batch_size=batch_size,
        shuffle=True,
        num_workers=2,
        pin_memory=torch.cuda.is_available(),
    )
    val_loader = DataLoader(
        WaferDataset(val_df, label_to_index),
        batch_size=batch_size,
        shuffle=False,
        num_workers=2,
        pin_memory=torch.cuda.is_available(),
    )

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"사용 장치: {device}")

    model = WaferCNN(num_classes=len(LABELS)).to(device)
    criterion = nn.CrossEntropyLoss()
    optimizer = torch.optim.Adam(model.parameters(), lr=learning_rate)

    best_state = None
    best_acc = 0.0

    for epoch in range(1, epochs + 1):
        model.train()
        total_loss = 0.0

        for images, targets in train_loader:
            images = images.to(device)
            targets = targets.to(device)

            optimizer.zero_grad()
            outputs = model(images)
            loss = criterion(outputs, targets)
            loss.backward()
            optimizer.step()

            total_loss += loss.item() * images.size(0)

        avg_loss = total_loss / len(train_loader.dataset)
        val_acc = accuracy(model, val_loader, device)
        print(f"Epoch {epoch:02d}/{epochs} | 손실={avg_loss:.4f} | 검증정확도={val_acc:.4f}")

        if val_acc >= best_acc:
            best_acc = val_acc
            best_state = {k: v.detach().cpu().clone() for k, v in model.state_dict().items()}

    if best_state is not None:
        model.load_state_dict(best_state)

    print(f"최고 검증 정확도: {best_acc:.4f}")
    return model.cpu()


def save_model(model: WaferCNN, output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)

    pth_path = output_dir / "wafer_defect_model.pth"
    onnx_path = output_dir / "wafer_defect_model.onnx"
    labels_path = output_dir / "wafer_labels.json"

    torch.save(
        {
            "model_state_dict": model.state_dict(),
            "labels": LABELS,
            "image_size": IMAGE_SIZE,
        },
        pth_path,
    )

    labels_path.write_text(json.dumps(LABELS, indent=2), encoding="utf-8")

    model.eval()
    dummy_input = torch.randn(1, 1, IMAGE_SIZE, IMAGE_SIZE)
    torch.onnx.export(
        model,
        dummy_input,
        onnx_path,
        export_params=True,
        opset_version=12,
        do_constant_folding=True,
        input_names=["input"],
        output_names=["output"],
        dynamic_axes={"input": {0: "batch_size"}, "output": {0: "batch_size"}},
    )

    print(f"저장 완료: {pth_path}")
    print(f"저장 완료: {onnx_path}")
    print(f"저장 완료: {labels_path}")


def save_wafer_png(wafer_map: object, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    plt.figure(figsize=(4, 4))
    plt.imshow(wafer_map, cmap="viridis")
    plt.axis("off")
    plt.tight_layout(pad=0)
    plt.savefig(path, bbox_inches="tight", pad_inches=0)
    plt.close()


def save_test_images(frame: pd.DataFrame, output_dir: Path) -> None:
    normal = frame[frame["failureType"] == "none"].iloc[0]["waferMap"]
    scratch = frame[frame["failureType"] == "Scratch"].iloc[0]["waferMap"]

    save_wafer_png(normal, output_dir / "normal_wafer.png")
    save_wafer_png(scratch, output_dir / "scratch_wafer.png")

    print(f"저장 완료: {output_dir / 'normal_wafer.png'}")
    print(f"저장 완료: {output_dir / 'scratch_wafer.png'}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-dir", default="/content/drive/MyDrive/Wafer-AOI-Project")
    parser.add_argument("--epochs", type=int, default=8)
    parser.add_argument("--batch-size", type=int, default=128)
    parser.add_argument("--learning-rate", type=float, default=0.001)
    parser.add_argument("--max-per-class", type=int, default=1500)
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    project_dir = Path(args.project_dir)
    dataset_dir = project_dir / "AI_Model" / "dataset"
    model_dir = project_dir / "AI_Model"
    test_images_dir = project_dir / "Assets" / "TestImages"

    pkl_path = ensure_dataset(dataset_dir)
    frame = load_balanced_frame(pkl_path, max_per_class=args.max_per_class)
    model = train(
        frame,
        epochs=args.epochs,
        batch_size=args.batch_size,
        learning_rate=args.learning_rate,
    )
    save_model(model, model_dir)
    save_test_images(frame, test_images_dir)


if __name__ == "__main__":
    main()
