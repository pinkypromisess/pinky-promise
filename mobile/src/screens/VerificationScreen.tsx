import { useEffect, useRef, useState } from 'react';
import { CameraView, useCameraPermissions } from 'expo-camera';
import { ActivityIndicator, Button, StyleSheet, Text, View } from 'react-native';
import type { NativeStackScreenProps } from '@react-navigation/native-stack';

import { getVerificationStatus, startVerification } from '../api/verification';
import type { Verification } from '../api/types';
import type { RootStackParamList } from '../navigation/types';

type Props = NativeStackScreenProps<RootStackParamList, 'Verification'>;

type Stage = 'capture' | 'submitting' | 'polling' | 'pass' | 'fail';

const POLL_INTERVAL_MS = 2000;

// ---------------------------------------------------------------------------
// BLOCKED: the real vendor integration (AWS Rekognition Face Liveness's own
// client SDK, driven by the `liveness_session_id` POST /verification returns)
// isn't wired up — it needs a native module outside Expo Go/managed workflow
// and AWS project config that doesn't exist in this environment. What's real
// here: the in-app camera capture (satisfies CUJ #2 — no gallery picker) and
// the actual POST /verification + GET /verification/status backend calls.
// The single `cameraRef.current.takePictureAsync()` call below stands in for
// the vendor SDK's multi-frame liveness session; swap it for the real
// <FaceLivenessDetector>-equivalent component once it's available, without
// touching the polling/result logic beneath it.
// ---------------------------------------------------------------------------

export default function VerificationScreen({ navigation }: Props) {
  const [permission, requestPermission] = useCameraPermissions();
  const cameraRef = useRef<CameraView>(null);

  const [stage, setStage] = useState<Stage>('capture');
  const [verification, setVerification] = useState<Verification | null>(null);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);

  useEffect(() => {
    if (stage !== 'polling') return;
    let cancelled = false;

    const interval = setInterval(async () => {
      try {
        const latest = await getVerificationStatus();
        if (cancelled) return;
        setVerification(latest);
        if (latest.decision === 'pass') {
          setStage('pass');
        } else if (latest.decision === 'fail') {
          setStage('fail');
        }
      } catch (err) {
        if (!cancelled) {
          setErrorMessage(err instanceof Error ? err.message : 'Failed to check verification status.');
        }
      }
    }, POLL_INTERVAL_MS);

    return () => {
      cancelled = true;
      clearInterval(interval);
    };
  }, [stage]);

  const handleCapture = async () => {
    setErrorMessage(null);
    setStage('submitting');
    try {
      await cameraRef.current?.takePictureAsync();
      const created = await startVerification();
      setVerification(created);
      setStage(created.decision === 'pending' ? 'polling' : created.decision === 'pass' ? 'pass' : 'fail');
    } catch (err) {
      setErrorMessage(err instanceof Error ? err.message : 'Failed to start verification.');
      setStage('capture');
    }
  };

  const handleRetry = () => {
    setVerification(null);
    setErrorMessage(null);
    setStage('capture');
  };

  if (!permission) {
    return (
      <View style={styles.center}>
        <ActivityIndicator size="large" />
      </View>
    );
  }

  if (!permission.granted) {
    return (
      <View style={styles.center}>
        <Text style={styles.message}>Camera access is required for selfie verification.</Text>
        <Button title="Grant Camera Permission" onPress={requestPermission} />
      </View>
    );
  }

  if (stage === 'pass') {
    return (
      <View testID="verification-result" style={styles.center}>
        <Text style={styles.resultTitlePass}>Verified!</Text>
        <Text style={styles.message}>You can now send Hearts and post Proposals.</Text>
        <Button title="Continue" onPress={() => navigation.replace('ProfileHome')} />
      </View>
    );
  }

  if (stage === 'fail') {
    return (
      <View testID="verification-result" style={styles.center}>
        <Text style={styles.resultTitleFail}>Verification failed</Text>
        <Text style={styles.message}>
          We couldn't confirm it's you from a live capture. Make sure you're in good lighting and try again.
        </Text>
        {errorMessage && <Text style={styles.error}>{errorMessage}</Text>}
        <Button title="Retry" onPress={handleRetry} />
      </View>
    );
  }

  if (stage === 'polling' || stage === 'submitting') {
    return (
      <View testID="verification-result" style={styles.center}>
        <ActivityIndicator size="large" />
        <Text style={styles.message}>
          {stage === 'submitting' ? 'Submitting your capture...' : 'Checking verification result...'}
        </Text>
      </View>
    );
  }

  return (
    <View style={styles.container}>
      <CameraView ref={cameraRef} style={styles.camera} facing="front" />
      {errorMessage && <Text style={styles.error}>{errorMessage}</Text>}
      <View style={styles.captureWrap}>
        <Text style={styles.message}>Center your face and take a live selfie.</Text>
        <Button title="Capture" onPress={handleCapture} />
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1 },
  camera: { flex: 1 },
  center: { flex: 1, alignItems: 'center', justifyContent: 'center', padding: 24 },
  captureWrap: { padding: 20, alignItems: 'center' },
  message: { textAlign: 'center', marginVertical: 12 },
  error: { color: '#b00020', textAlign: 'center', marginHorizontal: 20 },
  resultTitlePass: { fontSize: 22, fontWeight: '700', color: '#1e7e34', marginBottom: 8 },
  resultTitleFail: { fontSize: 22, fontWeight: '700', color: '#b00020', marginBottom: 8 },
});
