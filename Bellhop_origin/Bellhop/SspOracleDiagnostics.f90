MODULE SspOracleDiagnostics

  ! Optional, request-driven SSP diagnostic. Ordinary Bellhop runs perform no
  ! diagnostic I/O. A request row selects the exact 1-based arrival-side
  ! segment, including either side of an SSP node.

  USE FatalError
  USE, INTRINSIC :: IEEE_ARITHMETIC, ONLY: IEEE_IS_FINITE
  USE sspMod, ONLY: EvaluateSSP, SSP, iSegz
  IMPLICIT NONE

  PRIVATE
  PUBLIC :: SspOracleRun

  CHARACTER ( LEN=* ), PARAMETER :: RequestVariable = 'BELLHOP_SSP_ORACLE_REQUEST'
  CHARACTER ( LEN=* ), PARAMETER :: DirectoryVariable = 'BELLHOP_SSP_ORACLE_DIR'
  CHARACTER ( LEN=* ), PARAMETER :: RequestHeader = 'depth_m,segment_index_1based'

CONTAINS

  SUBROUTINE SspOracleRun( Frequency )

    REAL ( KIND=8 ), INTENT( IN ) :: Frequency
    CHARACTER ( LEN=1024 ) :: RequestPath, OutputDirectory, OutputPath, ManifestPath, Line
    INTEGER :: RequestLength, RequestStatus, DirectoryLength, DirectoryStatus
    INTEGER :: RequestUnit, OutputUnit, ManifestUnit, OpenStatus, ReadStatus
    INTEGER :: RequestedSegment, SampleCount, SavedSegment
    REAL ( KIND=8 ) :: Depth, Position( 2 ), SoundSpeed, ImaginarySoundSpeed
    REAL ( KIND=8 ) :: Gradient( 2 ), Crr, Crz, Czz, Density

    RequestPath = ''
    OutputDirectory = ''
    CALL GET_ENVIRONMENT_VARIABLE( RequestVariable, RequestPath, RequestLength, RequestStatus )
    CALL GET_ENVIRONMENT_VARIABLE( DirectoryVariable, OutputDirectory, DirectoryLength, DirectoryStatus )

    IF ( RequestStatus /= 0 .AND. DirectoryStatus /= 0 ) RETURN
    IF ( RequestStatus /= 0 .OR. RequestLength <= 0 .OR. DirectoryStatus /= 0 .OR. DirectoryLength <= 0 ) THEN
       CALL ERROUT( 'SspOracleRun', &
            'BELLHOP_SSP_ORACLE_REQUEST and BELLHOP_SSP_ORACLE_DIR must be set together' )
    END IF
    IF ( SSP%Type /= 'C' .AND. SSP%Type /= 'N' .AND. SSP%Type /= 'P' .AND. SSP%Type /= 'S' ) &
         CALL ERROUT( 'SspOracleRun', 'SSP oracle supports only range-independent C/N/P/S profiles' )

    SavedSegment = iSegz

    OPEN( NEWUNIT=RequestUnit, FILE=TRIM( RequestPath ), STATUS='OLD', ACTION='READ', IOSTAT=OpenStatus )
    IF ( OpenStatus /= 0 ) CALL ERROUT( 'SspOracleRun', 'Cannot open SSP oracle request CSV' )

    CALL JoinPath( TRIM( OutputDirectory ), 'ssp_samples.csv', OutputPath )
    OPEN( NEWUNIT=OutputUnit, FILE=TRIM( OutputPath ), STATUS='REPLACE', ACTION='WRITE', IOSTAT=OpenStatus )
    IF ( OpenStatus /= 0 ) CALL ERROUT( 'SspOracleRun', &
         'Cannot open SSP oracle output; BELLHOP_SSP_ORACLE_DIR must already exist and be writable' )

    READ( RequestUnit, '( A )', IOSTAT=ReadStatus ) Line
    IF ( ReadStatus /= 0 .OR. TRIM( Line ) /= RequestHeader ) &
         CALL ERROUT( 'SspOracleRun', 'Invalid SSP oracle request CSV header' )

    WRITE( OutputUnit, '( A )' ) &
         'depth_m,segment_index_1based,c_m_per_s,cimag_m_per_s,cz_s_inv,czz_m_inv_s_inv,rho_kg_m3'
    SampleCount = 0
    DO
       READ( RequestUnit, '( A )', IOSTAT=ReadStatus ) Line
       IF ( ReadStatus < 0 ) EXIT
       IF ( ReadStatus > 0 ) CALL ERROUT( 'SspOracleRun', 'Cannot read SSP oracle request CSV' )
       IF ( LEN_TRIM( Line ) == 0 ) CYCLE

       READ( Line, *, IOSTAT=ReadStatus ) Depth, RequestedSegment
       IF ( ReadStatus /= 0 ) CALL ERROUT( 'SspOracleRun', 'Invalid SSP oracle request row' )
       IF ( .NOT. IEEE_IS_FINITE( Depth ) ) CALL ERROUT( 'SspOracleRun', 'SSP oracle depth must be finite' )
       IF ( RequestedSegment < 1 .OR. RequestedSegment >= SSP%NPts ) &
            CALL ERROUT( 'SspOracleRun', 'SSP oracle segment index is out of range' )

       iSegz = RequestedSegment
       Position = [ 0.0D0, Depth ]
       CALL EvaluateSSP( Position, SoundSpeed, ImaginarySoundSpeed, Gradient, Crr, Crz, Czz, Density, &
            Frequency, 'TAB' )
       IF ( iSegz /= RequestedSegment ) CALL ERROUT( 'SspOracleRun', &
            'SSP oracle depth does not belong to the requested arrival-side segment' )

       WRITE( OutputUnit, '( ES25.17E3, ",", I0, 5( ",", ES25.17E3 ) )' ) &
            Depth, RequestedSegment, SoundSpeed, ImaginarySoundSpeed, Gradient( 2 ), Czz, 1000.0D0 * Density
       SampleCount = SampleCount + 1
    END DO
    CLOSE( RequestUnit )
    CLOSE( OutputUnit )

    CALL JoinPath( TRIM( OutputDirectory ), 'ssp_manifest.json', ManifestPath )
    OPEN( NEWUNIT=ManifestUnit, FILE=TRIM( ManifestPath ), STATUS='REPLACE', ACTION='WRITE', IOSTAT=OpenStatus )
    IF ( OpenStatus /= 0 ) CALL ERROUT( 'SspOracleRun', 'Cannot open SSP oracle manifest' )
    WRITE( ManifestUnit, '( A )' ) '{'
    WRITE( ManifestUnit, '( A )' ) '  "schema": "bellhop.fortran.ssp_oracle",'
    WRITE( ManifestUnit, '( A )' ) '  "version": 2,'
    WRITE( ManifestUnit, '( A, A, A )' ) '  "interpolation": "', SSP%Type, '",'
    WRITE( ManifestUnit, '( A, ES25.17E3, A )' ) '  "frequency_hz": ', Frequency, ','
    WRITE( ManifestUnit, '( A, I0 )' ) '  "sample_count": ', SampleCount
    WRITE( ManifestUnit, '( A )' ) '}'
    CLOSE( ManifestUnit )

    ! Do not leak the final requested arrival-side hint into ray tracing.
    iSegz = SavedSegment
  END SUBROUTINE SspOracleRun

  SUBROUTINE JoinPath( Directory, Leaf, Result )
    CHARACTER ( LEN=* ), INTENT( IN ) :: Directory, Leaf
    CHARACTER ( LEN=* ), INTENT( OUT ) :: Result
    INTEGER :: Length

    Length = LEN_TRIM( Directory )
    IF ( Length > 0 .AND. Directory( Length : Length ) == '/' ) THEN
       Result = TRIM( Directory ) // Leaf
    ELSE
       Result = TRIM( Directory ) // '/' // Leaf
    END IF
  END SUBROUTINE JoinPath

END MODULE SspOracleDiagnostics
