using Microsoft.ML.OnnxRuntime;
using Microsoft.ML.OnnxRuntime.Tensors;
using System.Collections.ObjectModel;
using System.IO;
using System.Text.Json;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace WaferUI
{
    public partial class MainWindow : Window
    {
        private const int ModelImageSize = 64;
        private readonly ObservableCollection<TestImageItem> _testImages = [];
        private readonly string _baseDirectory = AppContext.BaseDirectory;
        private InferenceSession? _session;
        private string[] _labels = [];
        private TestImageItem? _selectedImage;

        public MainWindow()
        {
            InitializeComponent();
            ImageListBox.ItemsSource = _testImages;
            Loaded += MainWindow_Loaded;
            Closed += (_, _) => _session?.Dispose();
        }

        private void MainWindow_Loaded(object sender, RoutedEventArgs e)
        {
            try
            {
                LoadModel();
                LoadTestImages();
                InferenceResultText.Text = "모델 로드 완료";
                AnalyzeButton.IsEnabled = _testImages.Count > 0;

                if (_testImages.Count > 0)
                {
                    ImageListBox.SelectedIndex = 0;
                }
            }
            catch (Exception ex)
            {
                AnalyzeButton.IsEnabled = false;
                InferenceResultText.Text = $"초기화 실패: {ex.Message}";
            }
        }

        private void RefreshImagesButton_Click(object sender, RoutedEventArgs e)
        {
            LoadTestImages();
            if (_testImages.Count > 0)
            {
                ImageListBox.SelectedIndex = 0;
                AnalyzeButton.IsEnabled = _session is not null;
            }
            else
            {
                _selectedImage = null;
                WaferImage.Source = null;
                SelectedImageText.Text = "이미지를 찾지 못했습니다";
                InferenceResultText.Text = "TestImages 폴더에 PNG/JPG 이미지를 추가하세요";
                AnalyzeButton.IsEnabled = false;
            }
        }

        private void ImageListBox_SelectionChanged(object sender, System.Windows.Controls.SelectionChangedEventArgs e)
        {
            _selectedImage = ImageListBox.SelectedItem as TestImageItem;
            if (_selectedImage is null)
            {
                return;
            }

            WaferImage.Source = LoadBitmap(_selectedImage.FullPath);
            SelectedImageText.Text = _selectedImage.FileName;
            InferenceResultText.Text = "분석 대기 중";
        }

        private void AnalyzeButton_Click(object sender, RoutedEventArgs e)
        {
            if (_session is null || _selectedImage is null)
            {
                InferenceResultText.Text = "모델 또는 이미지가 준비되지 않았습니다";
                return;
            }

            try
            {
                PredictionResult result = Predict(_selectedImage.FullPath);
                string defectText = result.IsDefect ? "불량" : "정상";
                InferenceResultText.Text =
                    $"판정: {defectText} / 클래스: {result.Label} / 신뢰도: {result.Confidence:P1}";
                InferenceResultText.Foreground = result.IsDefect
                    ? new SolidColorBrush(Color.FromRgb(190, 18, 60))
                    : new SolidColorBrush(Color.FromRgb(22, 101, 52));
            }
            catch (Exception ex)
            {
                InferenceResultText.Foreground = new SolidColorBrush(Color.FromRgb(190, 18, 60));
                InferenceResultText.Text = $"분석 실패: {ex.Message}";
            }
        }

        private void LoadModel()
        {
            string modelPath = Path.Combine(_baseDirectory, "AI_Model", "wafer_defect_model.onnx");
            string labelsPath = Path.Combine(_baseDirectory, "AI_Model", "wafer_labels.json");

            if (!File.Exists(modelPath))
            {
                throw new FileNotFoundException("ONNX 모델 파일을 찾지 못했습니다.", modelPath);
            }

            if (!File.Exists(labelsPath))
            {
                throw new FileNotFoundException("라벨 파일을 찾지 못했습니다.", labelsPath);
            }

            _labels = JsonSerializer.Deserialize<string[]>(File.ReadAllText(labelsPath)) ?? [];
            _session = new InferenceSession(modelPath);
        }

        private void LoadTestImages()
        {
            _testImages.Clear();

            string imageDirectory = Path.Combine(_baseDirectory, "TestImages");
            if (!Directory.Exists(imageDirectory))
            {
                imageDirectory = Path.GetFullPath(Path.Combine(_baseDirectory, "..", "..", "..", "TestImages"));
            }

            if (!Directory.Exists(imageDirectory))
            {
                return;
            }

            foreach (string path in Directory.EnumerateFiles(imageDirectory)
                         .Where(IsSupportedImage)
                         .OrderBy(Path.GetFileName))
            {
                _testImages.Add(new TestImageItem(path));
            }
        }

        private PredictionResult Predict(string imagePath)
        {
            DenseTensor<float> inputTensor = CreateInputTensor(imagePath);
            string inputName = _session!.InputMetadata.Keys.First();

            using IDisposableReadOnlyCollection<DisposableNamedOnnxValue> outputs =
                _session.Run([NamedOnnxValue.CreateFromTensor(inputName, inputTensor)]);

            float[] scores = outputs.First().AsEnumerable<float>().ToArray();
            int bestIndex = Array.IndexOf(scores, scores.Max());
            string label = bestIndex >= 0 && bestIndex < _labels.Length ? _labels[bestIndex] : $"Class {bestIndex}";
            float confidence = SoftmaxConfidence(scores, bestIndex);

            return new PredictionResult(label, confidence, !label.Equals("none", StringComparison.OrdinalIgnoreCase));
        }

        private static DenseTensor<float> CreateInputTensor(string imagePath)
        {
            BitmapSource source = LoadBitmap(imagePath);
            FormatConvertedBitmap converted = new(source, PixelFormats.Bgra32, null, 0);
            TransformedBitmap resized = new(
                converted,
                new ScaleTransform(
                    (double)ModelImageSize / converted.PixelWidth,
                    (double)ModelImageSize / converted.PixelHeight));

            int stride = resized.PixelWidth * 4;
            byte[] pixels = new byte[stride * resized.PixelHeight];
            resized.CopyPixels(pixels, stride, 0);

            DenseTensor<float> tensor = new([1, 1, ModelImageSize, ModelImageSize]);

            for (int y = 0; y < ModelImageSize; y++)
            {
                for (int x = 0; x < ModelImageSize; x++)
                {
                    int offset = (y * stride) + (x * 4);
                    byte blue = pixels[offset];
                    byte green = pixels[offset + 1];
                    byte red = pixels[offset + 2];
                    tensor[0, 0, y, x] = MapViridisWaferColorToModelValue(red, green, blue);
                }
            }

            return tensor;
        }

        private static float MapViridisWaferColorToModelValue(byte red, byte green, byte blue)
        {
            (byte Red, byte Green, byte Blue, float Value)[] colors =
            [
                (68, 1, 84, 0.0f),
                (32, 144, 140, 0.5f),
                (253, 231, 36, 1.0f)
            ];

            return colors
                .OrderBy(color => ColorDistanceSquared(red, green, blue, color.Red, color.Green, color.Blue))
                .First()
                .Value;
        }

        private static int ColorDistanceSquared(byte red, byte green, byte blue, byte targetRed, byte targetGreen, byte targetBlue)
        {
            int dr = red - targetRed;
            int dg = green - targetGreen;
            int db = blue - targetBlue;
            return (dr * dr) + (dg * dg) + (db * db);
        }

        private static float SoftmaxConfidence(float[] scores, int bestIndex)
        {
            if (scores.Length == 0 || bestIndex < 0)
            {
                return 0.0f;
            }

            float max = scores.Max();
            double sum = scores.Sum(score => Math.Exp(score - max));
            return (float)(Math.Exp(scores[bestIndex] - max) / sum);
        }

        private static BitmapImage LoadBitmap(string path)
        {
            BitmapImage bitmap = new();
            bitmap.BeginInit();
            bitmap.CacheOption = BitmapCacheOption.OnLoad;
            bitmap.UriSource = new Uri(path, UriKind.Absolute);
            bitmap.EndInit();
            bitmap.Freeze();
            return bitmap;
        }

        private static bool IsSupportedImage(string path)
        {
            string extension = Path.GetExtension(path);
            return extension.Equals(".png", StringComparison.OrdinalIgnoreCase)
                || extension.Equals(".jpg", StringComparison.OrdinalIgnoreCase)
                || extension.Equals(".jpeg", StringComparison.OrdinalIgnoreCase)
                || extension.Equals(".bmp", StringComparison.OrdinalIgnoreCase);
        }
    }

    public sealed record TestImageItem(string FullPath)
    {
        public string FileName => Path.GetFileName(FullPath);
    }

    public sealed record PredictionResult(string Label, float Confidence, bool IsDefect);
}
