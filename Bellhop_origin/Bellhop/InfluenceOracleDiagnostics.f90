MODULE InfluenceOracleDiagnostics

  ! Optional diagnostics for one Cartesian Cerveny ray evaluated at one
  ! receiver.  The numerical Influence module owns the request/result types;
  ! this module only selects, validates, and serializes them.  Unless all five
  ! BELLHOP_INFLUENCE_ORACLE_* selectors are set, every public procedure is a
  ! no-op and no file is opened.

  USE FatalError, ONLY: ERROUT
  USE Influence, ONLY: InfluenceOracleRequest, InfluenceOracleResult
  USE, INTRINSIC :: IEEE_ARITHMETIC, ONLY: IEEE_IS_FINITE
  IMPLICIT NONE
  PRIVATE

  INTEGER, PARAMETER :: SchemaVersion = 1
  CHARACTER (LEN=*), PARAMETER :: SchemaName = &
       'bellhop.fortran.cartesian_cerveny_influence_sample'
  CHARACTER (LEN=*), PARAMETER :: ImagesFileName = 'influence_images.csv'
  CHARACTER (LEN=*), PARAMETER :: ManifestFileName = 'influence_manifest.json'

  LOGICAL :: Configured = .FALSE.
  LOGICAL :: PendingRay = .FALSE.
  LOGICAL :: SelectedRayPrepared = .FALSE.
  LOGICAL :: ResultRecorded = .FALSE.
  LOGICAL :: Finalized = .FALSE.
  INTEGER :: SelectedSource = 0
  INTEGER :: SelectedAlpha = 0
  INTEGER :: SelectedRange = 0
  INTEGER :: SelectedDepth = 0
  INTEGER :: SourceCount = 0
  INTEGER :: AlphaCount = 0
  INTEGER :: RangeCount = 0
  INTEGER :: DepthCount = 0
  INTEGER :: CurrentSource = 0
  INTEGER :: CurrentAlpha = 0
  INTEGER :: EvaluationCount = 0
  INTEGER :: ImageRowCount = 0
  INTEGER :: ConfiguredNImage = 0
  INTEGER :: ConfiguredBeamWindow2 = 0
  REAL (KIND=8) :: CurrentAlphaRad = 0.0D0
  REAL (KIND=8) :: ConfiguredFrequencyHz = 0.0D0
  REAL (KIND=8) :: ConfiguredSourceSoundSpeed = 0.0D0
  REAL (KIND=8) :: ConfiguredDalphaRad = 0.0D0
  REAL (KIND=8) :: ConfiguredRLoopKm = 0.0D0
  REAL (KIND=8) :: ConfiguredEpsMultiplier = 0.0D0
  REAL (KIND=8) :: ConfiguredRadiusMaxM = 0.0D0
  REAL (KIND=8) :: ConfiguredSeaSurfaceDepthM = 0.0D0
  REAL (KIND=8) :: ConfiguredSeabedDepthM = 0.0D0
  REAL (KIND=8) :: SelectedReceiverRangeM = 0.0D0
  REAL (KIND=8) :: SelectedReceiverDepthM = 0.0D0
  COMPLEX (KIND=8) :: ConfiguredEpsilon = ( 0.0D0, 0.0D0 )
  CHARACTER (LEN=7) :: ConfiguredRunType = ''
  CHARACTER (LEN=4) :: ConfiguredBeamType = ''
  CHARACTER (LEN=256) :: CurrentFileRoot = ''
  CHARACTER (LEN=1024) :: OutputDirectory = ''

  PUBLIC :: InfluenceOracleInitialize
  PUBLIC :: InfluenceOraclePrepareRay
  PUBLIC :: InfluenceOracleIsPending
  PUBLIC :: InfluenceOracleBuildRequest
  PUBLIC :: InfluenceOracleRecordResult
  PUBLIC :: InfluenceOracleFinalize

CONTAINS

  SUBROUTINE InfluenceOracleInitialize( FileRoot, NSources, Nalpha, NRanges, NDepths )

    CHARACTER (LEN=*), INTENT( IN ) :: FileRoot
    INTEGER, INTENT( IN ) :: NSources, Nalpha, NRanges, NDepths
    CHARACTER (LEN=1024) :: Values( 5 )
    CHARACTER (LEN=48), PARAMETER :: Names( 5 ) = [ CHARACTER (LEN=48) :: &
         'BELLHOP_INFLUENCE_ORACLE_DIR', &
         'BELLHOP_INFLUENCE_ORACLE_SOURCE', &
         'BELLHOP_INFLUENCE_ORACLE_ALPHA', &
         'BELLHOP_INFLUENCE_ORACLE_RECEIVER_RANGE', &
         'BELLHOP_INFLUENCE_ORACLE_RECEIVER_DEPTH' ]
    INTEGER :: Lengths( 5 ), Statuses( 5 )
    INTEGER :: j, PresentCount

    IF ( Configured ) THEN
       CALL ERROUT( 'InfluenceOracleInitialize', 'Influence oracle was initialized more than once' )
    END IF

    Values = ''
    Lengths = 0
    Statuses = 0
    DO j = 1, SIZE( Names )
       CALL GET_ENVIRONMENT_VARIABLE( TRIM( Names( j ) ), Values( j ), &
            LENGTH=Lengths( j ), STATUS=Statuses( j ) )
       IF ( Statuses( j ) == 2 ) THEN
          CALL ERROUT( 'InfluenceOracleInitialize', &
               'Environment variables are not supported by this runtime' )
       END IF
       IF ( Statuses( j ) == -1 .OR. Lengths( j ) > LEN( Values( j ) ) ) THEN
          CALL ERROUT( 'InfluenceOracleInitialize', TRIM( Names( j ) ) // ' is too long' )
       END IF
       IF ( Statuses( j ) /= 0 .AND. Statuses( j ) /= 1 ) THEN
          CALL ERROUT( 'InfluenceOracleInitialize', &
               'Cannot read environment variable ' // TRIM( Names( j ) ) )
       END IF
    END DO

    PresentCount = COUNT( Lengths > 0 )
    IF ( PresentCount == 0 ) RETURN
    IF ( PresentCount /= SIZE( Names ) ) THEN
       CALL ERROUT( 'InfluenceOracleInitialize', &
            'Set all five BELLHOP_INFLUENCE_ORACLE_* variables, or set none' )
    END IF

    IF ( NSources < 1 .OR. Nalpha < 1 .OR. NRanges < 1 .OR. NDepths < 1 ) THEN
       CALL ERROUT( 'InfluenceOracleInitialize', 'Solver dimensions must all be positive' )
    END IF
    IF ( LEN_TRIM( FileRoot ) == 0 ) THEN
       CALL ERROUT( 'InfluenceOracleInitialize', 'FileRoot must not be empty' )
    END IF
    IF ( LEN_TRIM( FileRoot ) > LEN( CurrentFileRoot ) ) THEN
       CALL ERROUT( 'InfluenceOracleInitialize', 'FileRoot is too long' )
    END IF

    OutputDirectory = Values( 1 )( 1 : Lengths( 1 ) )
    CALL ParseIndex( Values( 2 )( 1 : Lengths( 2 ) ), Names( 2 ), NSources, SelectedSource )
    CALL ParseIndex( Values( 3 )( 1 : Lengths( 3 ) ), Names( 3 ), Nalpha, SelectedAlpha )
    CALL ParseIndex( Values( 4 )( 1 : Lengths( 4 ) ), Names( 4 ), NRanges, SelectedRange )
    CALL ParseIndex( Values( 5 )( 1 : Lengths( 5 ) ), Names( 5 ), NDepths, SelectedDepth )

    SourceCount = NSources
    AlphaCount = Nalpha
    RangeCount = NRanges
    DepthCount = NDepths
    CurrentFileRoot = FileRoot
    Configured = .TRUE.

  END SUBROUTINE InfluenceOracleInitialize

  SUBROUTINE InfluenceOraclePrepareRay( SourceIndex, AlphaIndex, AlphaRad )

    INTEGER, INTENT( IN ) :: SourceIndex, AlphaIndex
    REAL (KIND=8), INTENT( IN ) :: AlphaRad

    IF ( .NOT. Configured ) RETURN
    IF ( Finalized ) CALL ERROUT( 'InfluenceOraclePrepareRay', 'Influence oracle is already finalized' )
    IF ( SourceIndex < 1 .OR. SourceIndex > SourceCount ) THEN
       CALL ERROUT( 'InfluenceOraclePrepareRay', 'SourceIndex is outside the initialized range' )
    END IF
    IF ( AlphaIndex < 1 .OR. AlphaIndex > AlphaCount ) THEN
       CALL ERROUT( 'InfluenceOraclePrepareRay', 'AlphaIndex is outside the initialized range' )
    END IF
    IF ( .NOT. IEEE_IS_FINITE( AlphaRad ) ) THEN
       CALL ERROUT( 'InfluenceOraclePrepareRay', 'AlphaRad must be finite' )
    END IF

    PendingRay = SourceIndex == SelectedSource .AND. AlphaIndex == SelectedAlpha
    IF ( PendingRay ) THEN
       IF ( SelectedRayPrepared ) THEN
          CALL ERROUT( 'InfluenceOraclePrepareRay', 'Selected source/alpha pair was prepared more than once' )
       END IF
       SelectedRayPrepared = .TRUE.
       CurrentSource = SourceIndex
       CurrentAlpha = AlphaIndex
       CurrentAlphaRad = AlphaRad
    END IF

  END SUBROUTINE InfluenceOraclePrepareRay

  LOGICAL FUNCTION InfluenceOracleIsPending()

    InfluenceOracleIsPending = Configured .AND. PendingRay .AND. .NOT. Finalized

  END FUNCTION InfluenceOracleIsPending

  SUBROUTINE InfluenceOracleBuildRequest( Request )

    TYPE ( InfluenceOracleRequest ), INTENT( OUT ) :: Request

    Request = InfluenceOracleRequest()
    IF ( .NOT. InfluenceOracleIsPending() ) RETURN
    Request%Enabled = .TRUE.
    Request%ReceiverRangeIndex = SelectedRange
    Request%ReceiverDepthIndex = SelectedDepth

  END SUBROUTINE InfluenceOracleBuildRequest

  SUBROUTINE InfluenceOracleRecordResult( Result, Epsilon, DalphaRad, RLoopKm, EpsMultiplier, &
       RadiusMaxM, IBeamWindow2, NImage, RunType, BeamType, FrequencyHz, SourceSoundSpeed, &
       SeaSurfaceDepthM, SeabedDepthM )

    TYPE ( InfluenceOracleResult ), INTENT( IN ) :: Result
    COMPLEX (KIND=8), INTENT( IN ) :: Epsilon
    REAL (KIND=8), INTENT( IN ) :: DalphaRad, RLoopKm, EpsMultiplier
    REAL (KIND=8), INTENT( IN ) :: RadiusMaxM, FrequencyHz, SourceSoundSpeed
    REAL (KIND=8), INTENT( IN ) :: SeaSurfaceDepthM, SeabedDepthM
    INTEGER, INTENT( IN ) :: IBeamWindow2, NImage
    CHARACTER (LEN=*), INTENT( IN ) :: RunType, BeamType
    CHARACTER (LEN=1200) :: ImagesPath
    INTEGER :: ImagesUnit, OpenStatus, j

    IF ( .NOT. Configured .OR. .NOT. PendingRay ) RETURN
    IF ( Finalized ) CALL ERROUT( 'InfluenceOracleRecordResult', 'Influence oracle is already finalized' )
    IF ( ResultRecorded ) CALL ERROUT( 'InfluenceOracleRecordResult', 'Selected result was recorded more than once' )

    CALL ValidateMetadata( Epsilon, DalphaRad, RLoopKm, EpsMultiplier, RadiusMaxM, &
         IBeamWindow2, NImage, RunType, BeamType, FrequencyHz, SourceSoundSpeed, &
         SeaSurfaceDepthM, SeabedDepthM )
    CALL ValidateResult( Result, NImage )
    IF ( ABS( Result%EpsilonLeft - Epsilon ) > &
         16.0D0 * SPACING( 1.0D0 ) * MAX( 1.0D0, ABS( Epsilon ) ) ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', &
            'Minimum-width result epsilon does not match PickEpsilon output' )
    END IF

    ConfiguredEpsilon = Epsilon
    ConfiguredDalphaRad = DalphaRad
    ConfiguredRLoopKm = RLoopKm
    ConfiguredEpsMultiplier = EpsMultiplier
    ConfiguredRadiusMaxM = RadiusMaxM
    ConfiguredSeaSurfaceDepthM = SeaSurfaceDepthM
    ConfiguredSeabedDepthM = SeabedDepthM
    ConfiguredBeamWindow2 = IBeamWindow2
    ConfiguredNImage = NImage
    ConfiguredRunType = RunType
    ConfiguredBeamType = BeamType
    ConfiguredFrequencyHz = FrequencyHz
    ConfiguredSourceSoundSpeed = SourceSoundSpeed
    SelectedReceiverRangeM = Result%ReceiverRange
    SelectedReceiverDepthM = Result%ReceiverDepth
    EvaluationCount = Result%EvaluationCount
    ImageRowCount = 0

    ImagesPath = JoinPath( OutputDirectory, ImagesFileName )
    OPEN( NEWUNIT=ImagesUnit, FILE=TRIM( ImagesPath ), STATUS='REPLACE', &
         ACTION='WRITE', IOSTAT=OpenStatus )
    IF ( OpenStatus /= 0 ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', &
            'Cannot open influence_images.csv; output directory must already exist and be writable' )
    END IF
    CALL WriteHeader( ImagesUnit )

    IF ( Result%Evaluated ) THEN
       DO j = 1, Result%ImageCount
          ImageRowCount = ImageRowCount + 1
          CALL WriteImageRow( ImagesUnit, j, Result )
       END DO
    END IF
    CLOSE( ImagesUnit )
    ResultRecorded = .TRUE.

  END SUBROUTINE InfluenceOracleRecordResult

  SUBROUTINE InfluenceOracleFinalize()

    CHARACTER (LEN=32) :: StatusValue

    IF ( .NOT. Configured .OR. Finalized ) RETURN

    IF ( ResultRecorded ) THEN
       StatusValue = 'complete'
    ELSE IF ( SelectedRayPrepared ) THEN
       StatusValue = 'selected_ray_not_recorded'
    ELSE
       StatusValue = 'selected_ray_not_traced'
    END IF

    CALL WriteManifest( StatusValue )
    PendingRay = .FALSE.
    Finalized = .TRUE.

  END SUBROUTINE InfluenceOracleFinalize

  SUBROUTINE ValidateMetadata( Epsilon, DalphaRad, RLoopKm, EpsMultiplier, RadiusMaxM, &
       IBeamWindow2, NImage, RunType, BeamType, FrequencyHz, SourceSoundSpeed, &
       SeaSurfaceDepthM, SeabedDepthM )

    COMPLEX (KIND=8), INTENT( IN ) :: Epsilon
    REAL (KIND=8), INTENT( IN ) :: DalphaRad, RLoopKm, EpsMultiplier
    REAL (KIND=8), INTENT( IN ) :: RadiusMaxM, FrequencyHz, SourceSoundSpeed
    REAL (KIND=8), INTENT( IN ) :: SeaSurfaceDepthM, SeabedDepthM
    INTEGER, INTENT( IN ) :: IBeamWindow2, NImage
    CHARACTER (LEN=*), INTENT( IN ) :: RunType, BeamType

    IF ( LEN( RunType ) < 4 .OR. RunType( 1 : 2 ) /= 'CC' ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', &
            'Influence oracle requires coherent Cartesian RunType CC' )
    END IF
    IF ( LEN( BeamType ) < 3 .OR. BeamType( 1 : 3 ) /= 'CMS' ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', &
            'Influence oracle requires Cartesian minimum-width/standard BeamType CMS' )
    END IF
    IF ( NImage < 1 .OR. NImage > 3 ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', 'NImage must be in the supported range 1 through 3' )
    END IF
    IF ( IBeamWindow2 <= 0 ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', 'IBeamWindow2 must be positive' )
    END IF
    IF ( .NOT. FiniteComplex( Epsilon ) ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', 'Epsilon must be finite' )
    END IF
    IF ( ABS( Epsilon ) < TINY( 1.0D0 ) ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', 'Epsilon must not be zero' )
    END IF
    IF ( .NOT. IEEE_IS_FINITE( DalphaRad ) .OR. DalphaRad <= 0.0D0 ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', 'DalphaRad must be finite and positive' )
    END IF
    IF ( .NOT. IEEE_IS_FINITE( RLoopKm ) .OR. RLoopKm <= 0.0D0 ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', 'RLoopKm must be finite and positive' )
    END IF
    IF ( .NOT. IEEE_IS_FINITE( EpsMultiplier ) .OR. EpsMultiplier <= 0.0D0 ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', 'EpsMultiplier must be finite and positive' )
    END IF
    IF ( .NOT. IEEE_IS_FINITE( RadiusMaxM ) .OR. RadiusMaxM <= 0.0D0 ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', 'RadiusMaxM must be finite and positive' )
    END IF
    IF ( .NOT. IEEE_IS_FINITE( FrequencyHz ) .OR. FrequencyHz <= 0.0D0 ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', 'FrequencyHz must be finite and positive' )
    END IF
    IF ( .NOT. IEEE_IS_FINITE( SourceSoundSpeed ) .OR. SourceSoundSpeed <= 0.0D0 ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', &
            'SourceSoundSpeed must be finite and positive' )
    END IF
    IF ( .NOT. IEEE_IS_FINITE( SeaSurfaceDepthM ) .OR. &
         .NOT. IEEE_IS_FINITE( SeabedDepthM ) .OR. SeaSurfaceDepthM >= SeabedDepthM ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', &
            'Sea-surface and seabed depths must be finite and strictly ordered' )
    END IF

  END SUBROUTINE ValidateMetadata

  SUBROUTINE ValidateResult( Result, NImage )

    TYPE ( InfluenceOracleResult ), INTENT( IN ) :: Result
    INTEGER, INTENT( IN ) :: NImage
    CHARACTER (LEN=7), PARAMETER :: ExpectedKinds( 3 ) = [ CHARACTER (LEN=7) :: &
         'true', 'surface', 'bottom' ]
    INTEGER :: j

    IF ( .NOT. Result%Evaluated .OR. Result%EvaluationCount /= 1 ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', &
            'Selected receiver must have exactly one retained range evaluation' )
    END IF

    IF ( Result%ReceiverRangeIndex /= SelectedRange .OR. &
         Result%ReceiverDepthIndex /= SelectedDepth ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', 'Result receiver does not match selected indices' )
    END IF
    IF ( Result%ReceiverRangeIndex < 1 .OR. Result%ReceiverRangeIndex > RangeCount .OR. &
         Result%ReceiverDepthIndex < 1 .OR. Result%ReceiverDepthIndex > DepthCount ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', 'Result receiver indices are outside initialized dimensions' )
    END IF
    IF ( Result%LeftPointIndex < 1 .OR. Result%RightPointIndex /= Result%LeftPointIndex + 1 ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', &
            'Result must identify consecutive positive Fortran point indices' )
    END IF
    IF ( .NOT. IEEE_IS_FINITE( Result%ReceiverRange ) .OR. &
         .NOT. IEEE_IS_FINITE( Result%ReceiverDepth ) ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', 'Receiver coordinates must be finite' )
    END IF
    IF ( .NOT. IEEE_IS_FINITE( Result%InterpolationWeight ) .OR. &
         Result%InterpolationWeight < 0.0D0 .OR. Result%InterpolationWeight > 1.0D0 ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', 'Interpolation weight must be finite and in [0, 1]' )
    END IF
    IF ( Result%KMAHLeft /= -1 .AND. Result%KMAHLeft /= 1 ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', 'KMAHLeft must be -1 or +1' )
    END IF
    IF ( Result%KMAHFinal /= -1 .AND. Result%KMAHFinal /= 1 ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', 'KMAHFinal must be -1 or +1' )
    END IF
    IF ( Result%ImageCount /= NImage ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', 'Result ImageCount does not match configured NImage' )
    END IF
    IF ( .NOT. FiniteResult( Result ) ) THEN
       CALL ERROUT( 'InfluenceOracleRecordResult', 'Influence result contains non-finite data' )
    END IF

    DO j = 1, Result%ImageCount
       IF ( TRIM( Result%Images( j )%Kind ) /= TRIM( ExpectedKinds( j ) ) ) THEN
          CALL ERROUT( 'InfluenceOracleRecordResult', 'Image kind/order does not match legacy image loop' )
       END IF
    END DO

  END SUBROUTINE ValidateResult

  SUBROUTINE WriteHeader( ImagesUnit )

    INTEGER, INTENT( IN ) :: ImagesUnit

    WRITE( ImagesUnit, '( A )' ) &
         'image_index,image_kind,left_point_index,right_point_index,interpolation_weight,' // &
         'interp_r_m,interp_z_m,interp_t_r_s_per_m,interp_t_z_s_per_m,interp_c_m_per_s,' // &
         'tau_real_s,tau_imag_s,q_left_real,q_left_imag,q_right_real,q_right_imag,q_real,q_imag,' // &
         'gamma_left_real,gamma_left_imag,gamma_right_real,gamma_right_imag,gamma_real,gamma_imag,' // &
         'kmah_left,kmah_interpolated,const_before_kmah_real,const_before_kmah_imag,const_real,' // &
         'const_imag,right_amplitude,right_phase_rad,delta_z_m,polarity,window_metric,window_pass,' // &
         'hermite_taper,image_contribution_real,image_contribution_imag,image_sum_real,image_sum_imag,' // &
         'ray_contribution_real,ray_contribution_imag,complex64_increment_real,complex64_increment_imag'

  END SUBROUTINE WriteHeader

  SUBROUTINE WriteImageRow( ImagesUnit, ImageIndex, Result )

    INTEGER, INTENT( IN ) :: ImagesUnit, ImageIndex
    TYPE ( InfluenceOracleResult ), INTENT( IN ) :: Result
    COMPLEX (KIND=8) :: QuantizedIncrement

    QuantizedIncrement = ToComplex8( Result%QuantizedIncrement )

    WRITE( ImagesUnit, '( I0, ",", A, ",", I0, ",", I0 )', ADVANCE='NO' ) &
         ImageIndex, TRIM( Result%Images( ImageIndex )%Kind ), &
         Result%LeftPointIndex, Result%RightPointIndex
    CALL WriteRealValue( ImagesUnit, Result%InterpolationWeight )
    CALL WriteRealArray( ImagesUnit, Result%InterpolatedPosition )
    CALL WriteRealArray( ImagesUnit, Result%InterpolatedSlowness )
    CALL WriteRealValue( ImagesUnit, Result%InterpolatedSoundSpeed )
    CALL WriteComplexValue( ImagesUnit, Result%TauInterpolated )
    CALL WriteComplexValue( ImagesUnit, Result%QVBLeft )
    CALL WriteComplexValue( ImagesUnit, Result%QVBRight )
    CALL WriteComplexValue( ImagesUnit, Result%QInterpolated )
    CALL WriteComplexValue( ImagesUnit, Result%GammaLeft )
    CALL WriteComplexValue( ImagesUnit, Result%GammaRight )
    CALL WriteComplexValue( ImagesUnit, Result%GammaInterpolated )
    CALL WriteIntegerValue( ImagesUnit, Result%KMAHLeft )
    CALL WriteIntegerValue( ImagesUnit, Result%KMAHFinal )
    CALL WriteComplexValue( ImagesUnit, Result%ConstantPrincipal )
    CALL WriteComplexValue( ImagesUnit, Result%ConstantCorrected )
    CALL WriteRealValue( ImagesUnit, Result%RightAmplitude )
    CALL WriteRealValue( ImagesUnit, Result%RightPhase )
    CALL WriteRealValue( ImagesUnit, Result%Images( ImageIndex )%DeltaZ )
    CALL WriteRealValue( ImagesUnit, Result%Images( ImageIndex )%Polarity )
    CALL WriteRealValue( ImagesUnit, Result%Images( ImageIndex )%WindowMetric )
    CALL WriteIntegerValue( ImagesUnit, MERGE( 1, 0, Result%Images( ImageIndex )%WindowPassed ) )
    CALL WriteRealValue( ImagesUnit, Result%Images( ImageIndex )%HermiteTaper )
    CALL WriteComplexValue( ImagesUnit, Result%Images( ImageIndex )%Contribution )
    CALL WriteComplexValue( ImagesUnit, Result%RawImageSum )
    CALL WriteComplexValue( ImagesUnit, Result%FinalContribution )
    CALL WriteComplexValue( ImagesUnit, QuantizedIncrement )
    WRITE( ImagesUnit, * )

  END SUBROUTINE WriteImageRow

  SUBROUTINE WriteManifest( StatusValue )

    CHARACTER (LEN=*), INTENT( IN ) :: StatusValue
    CHARACTER (LEN=1200) :: ManifestPath
    INTEGER :: ManifestUnit, OpenStatus

    ManifestPath = JoinPath( OutputDirectory, ManifestFileName )
    OPEN( NEWUNIT=ManifestUnit, FILE=TRIM( ManifestPath ), STATUS='REPLACE', &
         ACTION='WRITE', IOSTAT=OpenStatus )
    IF ( OpenStatus /= 0 ) THEN
       CALL ERROUT( 'InfluenceOracleFinalize', 'Cannot open influence_manifest.json for writing' )
    END IF

    WRITE( ManifestUnit, '( A )' ) '{'
    WRITE( ManifestUnit, '( A )' ) '  "schema": "' // SchemaName // '",'
    WRITE( ManifestUnit, '( A, I0, A )' ) '  "schema_version": ', SchemaVersion, ','
    WRITE( ManifestUnit, '( A )' ) '  "status": "' // TRIM( StatusValue ) // '",'
    WRITE( ManifestUnit, '( A )' ) '  "file_root": "' // TRIM( EscapeJson( CurrentFileRoot ) ) // '",'
    WRITE( ManifestUnit, '( A, I0, A )' ) '  "source_index": ', SelectedSource, ','
    WRITE( ManifestUnit, '( A, I0, A )' ) '  "launch_angle_index": ', SelectedAlpha, ','
    WRITE( ManifestUnit, '( A, I0, A )' ) '  "receiver_range_index": ', SelectedRange, ','
    WRITE( ManifestUnit, '( A, I0, A )' ) '  "receiver_depth_index": ', SelectedDepth, ','
    WRITE( ManifestUnit, '( A, ES26.17E3, A )' ) '  "launch_angle_rad": ', CurrentAlphaRad, ','
    WRITE( ManifestUnit, '( A, ES26.17E3, A )' ) &
         '  "receiver_range_m": ', SelectedReceiverRangeM, ','
    WRITE( ManifestUnit, '( A, ES26.17E3, A )' ) &
         '  "receiver_depth_m": ', SelectedReceiverDepthM, ','
    WRITE( ManifestUnit, '( A, ES26.17E3, A )' ) &
         '  "sea_surface_depth_m": ', ConfiguredSeaSurfaceDepthM, ','
    WRITE( ManifestUnit, '( A, ES26.17E3, A )' ) &
         '  "seabed_depth_m": ', ConfiguredSeabedDepthM, ','
    WRITE( ManifestUnit, '( A, ES26.17E3, A )' ) '  "frequency_hz": ', ConfiguredFrequencyHz, ','
    WRITE( ManifestUnit, '( A, ES26.17E3, A )' ) &
         '  "source_sound_speed_m_per_s": ', ConfiguredSourceSoundSpeed, ','
    WRITE( ManifestUnit, '( A, ES26.17E3, A )' ) '  "dalpha_rad": ', ConfiguredDalphaRad, ','
    WRITE( ManifestUnit, '( A, ES26.17E3, A )' ) '  "rloop_km": ', ConfiguredRLoopKm, ','
    WRITE( ManifestUnit, '( A, ES26.17E3, A )' ) &
         '  "epsilon_multiplier": ', ConfiguredEpsMultiplier, ','
    WRITE( ManifestUnit, '( A, ES26.17E3, A )' ) '  "epsilon_real": ', &
         REAL( ConfiguredEpsilon, KIND=8 ), ','
    WRITE( ManifestUnit, '( A, ES26.17E3, A )' ) '  "epsilon_imag": ', &
         AIMAG( ConfiguredEpsilon ), ','
    IF ( ConfiguredRunType( 4 : 4 ) == 'X' ) THEN
       WRITE( ManifestUnit, '( A, ES26.17E3, A )' ) '  "ratio1": ', 1.0D0, ','
    ELSE
       WRITE( ManifestUnit, '( A, ES26.17E3, A )' ) &
            '  "ratio1": ', SQRT( ABS( COS( CurrentAlphaRad ) ) ), ','
    END IF
    WRITE( ManifestUnit, '( A, ES26.17E3, A )' ) '  "radius_max_m": ', ConfiguredRadiusMaxM, ','
    WRITE( ManifestUnit, '( A, I0, A )' ) '  "beam_window_squared": ', ConfiguredBeamWindow2, ','
    WRITE( ManifestUnit, '( A, I0, A )' ) '  "image_count": ', ConfiguredNImage, ','
    WRITE( ManifestUnit, '( A )' ) '  "run_type": "' // TRIM( EscapeJson( ConfiguredRunType ) ) // '",'
    WRITE( ManifestUnit, '( A )' ) '  "beam_type": "' // TRIM( EscapeJson( ConfiguredBeamType ) ) // '",'
    WRITE( ManifestUnit, '( A )' ) '  "beam_width_mode": "minimum",'
    WRITE( ManifestUnit, '( A )' ) &
         '  "contribution_stage": "complex128_before_complex64_accumulation_and_scale_pressure",'
    WRITE( ManifestUnit, '( A )' ) '  "images_file": "' // ImagesFileName // '",'
    WRITE( ManifestUnit, '( A )' ) '  "ray_points_file": "ray_points.csv",'
    WRITE( ManifestUnit, '( A, I0, A )' ) &
         '  "selected_range_evaluation_count": ', EvaluationCount, ','
    WRITE( ManifestUnit, '( A, I0, A )' ) '  "evaluation_count": ', EvaluationCount, ','
    WRITE( ManifestUnit, '( A, I0, A )' ) '  "image_row_count": ', ImageRowCount, ','
    WRITE( ManifestUnit, '( A )' ) '  "index_base": 1,'
    WRITE( ManifestUnit, '( A )' ) '  "limitations": ['
    WRITE( ManifestUnit, '( A )' ) &
         '    "only coherent Cartesian Cerveny with minimum-width epsilon and standard curvature is accepted",'
    WRITE( ManifestUnit, '( A )' ) &
         '    "when evaluation_count exceeds one, the CSV contains the first matching segment retained by Influence",'
    WRITE( ManifestUnit, '( A )' ) &
         '    "quantized_increment records the legacy default-complex conversion before field accumulation",'
    WRITE( ManifestUnit, '( A )' ) &
         '    "final_contribution is recorded before ScalePressure scales the accumulated single-precision field"'
    WRITE( ManifestUnit, '( A )' ) '  ]'
    WRITE( ManifestUnit, '( A )' ) '}'
    CLOSE( ManifestUnit )

  END SUBROUTINE WriteManifest

  COMPLEX (KIND=8) FUNCTION ToComplex8( Value )

    COMPLEX (KIND=4), INTENT( IN ) :: Value

    ToComplex8 = CMPLX( REAL( Value, KIND=8 ), REAL( AIMAG( Value ), KIND=8 ), KIND=8 )

  END FUNCTION ToComplex8

  SUBROUTINE WriteRealArray( UnitNumber, Values )

    INTEGER, INTENT( IN ) :: UnitNumber
    REAL (KIND=8), INTENT( IN ) :: Values( : )
    INTEGER :: j

    DO j = 1, SIZE( Values )
       CALL WriteRealValue( UnitNumber, Values( j ) )
    END DO

  END SUBROUTINE WriteRealArray

  SUBROUTINE WriteRealValue( UnitNumber, Value )

    INTEGER, INTENT( IN ) :: UnitNumber
    REAL (KIND=8), INTENT( IN ) :: Value

    WRITE( UnitNumber, '( ",", ES26.17E3 )', ADVANCE='NO' ) Value

  END SUBROUTINE WriteRealValue

  SUBROUTINE WriteComplexValue( UnitNumber, Value )

    INTEGER, INTENT( IN ) :: UnitNumber
    COMPLEX (KIND=8), INTENT( IN ) :: Value

    CALL WriteRealValue( UnitNumber, REAL( Value, KIND=8 ) )
    CALL WriteRealValue( UnitNumber, AIMAG( Value ) )

  END SUBROUTINE WriteComplexValue

  SUBROUTINE WriteIntegerValue( UnitNumber, Value )

    INTEGER, INTENT( IN ) :: UnitNumber, Value

    WRITE( UnitNumber, '( ",", I0 )', ADVANCE='NO' ) Value

  END SUBROUTINE WriteIntegerValue

  LOGICAL FUNCTION FiniteComplex( Value )

    COMPLEX (KIND=8), INTENT( IN ) :: Value

    FiniteComplex = IEEE_IS_FINITE( REAL( Value, KIND=8 ) ) .AND. &
         IEEE_IS_FINITE( AIMAG( Value ) )

  END FUNCTION FiniteComplex

  LOGICAL FUNCTION FiniteResult( Result )

    TYPE ( InfluenceOracleResult ), INTENT( IN ) :: Result
    INTEGER :: j

    FiniteResult = ALL( IEEE_IS_FINITE( Result%InterpolatedPosition ) ) .AND. &
         ALL( IEEE_IS_FINITE( Result%InterpolatedSlowness ) ) .AND. &
         IEEE_IS_FINITE( Result%InterpolatedSoundSpeed ) .AND. &
         IEEE_IS_FINITE( Result%RightAmplitude ) .AND. IEEE_IS_FINITE( Result%RightPhase ) .AND. &
         FiniteComplex( Result%EpsilonLeft ) .AND. FiniteComplex( Result%PVBLeft ) .AND. &
         FiniteComplex( Result%PVBRight ) .AND. FiniteComplex( Result%QVBLeft ) .AND. &
         FiniteComplex( Result%QVBRight ) .AND. FiniteComplex( Result%QInterpolated ) .AND. &
         FiniteComplex( Result%TauInterpolated ) .AND. FiniteComplex( Result%GammaLeft ) .AND. &
         FiniteComplex( Result%GammaRight ) .AND. FiniteComplex( Result%GammaInterpolated ) .AND. &
         FiniteComplex( Result%ConstantPrincipal ) .AND. &
         FiniteComplex( Result%ConstantCorrected ) .AND. FiniteComplex( Result%RawImageSum ) .AND. &
         FiniteComplex( Result%FinalContribution ) .AND. &
         IEEE_IS_FINITE( REAL( Result%QuantizedIncrement ) ) .AND. &
         IEEE_IS_FINITE( AIMAG( Result%QuantizedIncrement ) )
    IF ( .NOT. FiniteResult ) RETURN
    IF ( Result%RightAmplitude < 0.0D0 ) THEN
       FiniteResult = .FALSE.
       RETURN
    END IF

    DO j = 1, Result%ImageCount
       FiniteResult = IEEE_IS_FINITE( Result%Images( j )%DeltaZ ) .AND. &
            IEEE_IS_FINITE( Result%Images( j )%Polarity ) .AND. &
            IEEE_IS_FINITE( Result%Images( j )%WindowMetric ) .AND. &
            IEEE_IS_FINITE( Result%Images( j )%HermiteTaper ) .AND. &
            FiniteComplex( Result%Images( j )%Exponential ) .AND. &
            FiniteComplex( Result%Images( j )%Contribution )
       IF ( .NOT. FiniteResult ) RETURN
       IF ( Result%Images( j )%HermiteTaper < 0.0D0 .OR. &
            Result%Images( j )%HermiteTaper > 1.0D0 ) THEN
          FiniteResult = .FALSE.
          RETURN
       END IF
    END DO

  END FUNCTION FiniteResult

  SUBROUTINE ParseIndex( Value, Name, UpperBound, Result )

    CHARACTER (LEN=*), INTENT( IN ) :: Value, Name
    INTEGER, INTENT( IN ) :: UpperBound
    INTEGER, INTENT( OUT ) :: Result
    CHARACTER (LEN=1024) :: Normalized
    INTEGER :: j, ParseStatus, CharacterCode, ValueLength

    Normalized = ADJUSTL( Value )
    ValueLength = LEN_TRIM( Normalized )
    IF ( ValueLength == 0 ) THEN
       CALL ERROUT( 'InfluenceOracleInitialize', TRIM( Name ) // ' must not be empty' )
    END IF
    DO j = 1, ValueLength
       CharacterCode = IACHAR( Normalized( j : j ) )
       IF ( CharacterCode < IACHAR( '0' ) .OR. CharacterCode > IACHAR( '9' ) ) THEN
          CALL ERROUT( 'InfluenceOracleInitialize', &
               TRIM( Name ) // ' must contain one unsigned decimal integer' )
       END IF
    END DO

    READ( Normalized( 1 : ValueLength ), *, IOSTAT=ParseStatus ) Result
    IF ( ParseStatus /= 0 .OR. Result < 1 .OR. Result > UpperBound ) THEN
       CALL ERROUT( 'InfluenceOracleInitialize', &
            TRIM( Name ) // ' must be a valid 1-based index' )
    END IF

  END SUBROUTINE ParseIndex

  CHARACTER (LEN=1200) FUNCTION JoinPath( Directory, FileName )

    CHARACTER (LEN=*), INTENT( IN ) :: Directory, FileName
    INTEGER :: Last

    Last = LEN_TRIM( Directory )
    IF ( Last > 0 .AND. Directory( Last : Last ) == '/' ) THEN
       JoinPath = TRIM( Directory ) // FileName
    ELSE
       JoinPath = TRIM( Directory ) // '/' // FileName
    END IF

  END FUNCTION JoinPath

  CHARACTER (LEN=600) FUNCTION EscapeJson( Input )

    CHARACTER (LEN=*), INTENT( IN ) :: Input
    CHARACTER (LEN=2) :: HexCode
    INTEGER :: j, OutIndex, CharacterCode

    EscapeJson = ''
    OutIndex = 0
    DO j = 1, LEN_TRIM( Input )
       CharacterCode = IACHAR( Input( j : j ) )
       SELECT CASE ( CharacterCode )
       CASE ( 0 : 31 )
          IF ( OutIndex + 6 > LEN( EscapeJson ) ) EXIT
          WRITE( HexCode, '( Z2.2 )' ) CharacterCode
          EscapeJson( OutIndex + 1 : OutIndex + 1 ) = ACHAR( 92 )
          EscapeJson( OutIndex + 2 : OutIndex + 4 ) = 'u00'
          EscapeJson( OutIndex + 5 : OutIndex + 6 ) = HexCode
          OutIndex = OutIndex + 6
       CASE ( 34, 92 )
          IF ( OutIndex + 2 > LEN( EscapeJson ) ) EXIT
          OutIndex = OutIndex + 1
          EscapeJson( OutIndex : OutIndex ) = ACHAR( 92 )
          OutIndex = OutIndex + 1
          EscapeJson( OutIndex : OutIndex ) = Input( j : j )
       CASE DEFAULT
          IF ( OutIndex + 1 > LEN( EscapeJson ) ) EXIT
          OutIndex = OutIndex + 1
          EscapeJson( OutIndex : OutIndex ) = Input( j : j )
       END SELECT
    END DO

  END FUNCTION EscapeJson

END MODULE InfluenceOracleDiagnostics
